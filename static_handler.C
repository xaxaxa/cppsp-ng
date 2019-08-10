#include <cppsp-ng/static_handler.H>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>


using namespace std;
using namespace CP;

namespace cppsp {
	struct LoadedStaticFile {
		// position in the to-free list
		LoadedStaticFile* next;
		LoadedStaticFile* prev;
		StaticFileManager* manager;
		string path;
		string mimeType;
		uint8_t* mapped;
		int fd;
		int length;
		int refCount;
	};
	struct FileRef {
		LoadedStaticFile* file;

		// if refcount becomes 0, put the file on the to-free list;
		// if refcount becomes nonzero, remove the file from the to-free list.

		FileRef(LoadedStaticFile* file):file(file) {
			if(file->refCount == 0)
				file->manager->retain(file);
			file->refCount++;
		}
		~FileRef() {
			file->refCount--;
			if(file->refCount == 0)
				file->manager->put(file);
		}
		LoadedStaticFile& operator*() {
			return *file;
		}
		LoadedStaticFile* operator->() {
			return file;
		}
	};
	struct StaticFileHandler {
		ConnectionHandler& ch;
		FileRef file;
		iovec iov[2];
		StaticFileHandler(ConnectionHandler& ch, LoadedStaticFile* file)
			: ch(ch), file(file) { }
		void start() {
			file->manager->requestsCounter++;
			auto& response = ch.response;
			response.addContentLengthHeader(file->length);
			// if we have the file mmap'd, we can use writev to send the
			// headers and contents at once; otherwise use sendfile()
			if(file->mapped) {
				iov[0].iov_base = response.headersBuffer.data();
				iov[0].iov_len = response.headersBuffer.length();
				iov[1].iov_base = file->mapped;
				iov[1].iov_len = file->length;
				ch.socket.writevAll(iov, 2, [this](int r) {
					this->~StaticFileHandler();
					if(r <= 0) ch.abort();
					else ch.finish(false);
				});
			} else {
				ch.socket.writeAll(response.headersBuffer.data(),
								response.headersBuffer.length(), [this](int r) {
					if(r != ch.response.headersBuffer.length()) {
						abort();
						return;
					}
					sendFileContents();
				});
			}
		}
		void sendFileContents() {
			ch.socket.repeatSendFileFrom(file->fd, -1, 1024*16, [this](int r) {
				if(r <= 0) finish();
			});
		}
		inline void abort() {
			this->~StaticFileHandler();
			ch.abort();
		}
		inline void finish() {
			this->~StaticFileHandler();
			ch.finish(false);
		}
	};

	StaticFileManager::StaticFileManager(string basePath): basePath(basePath) {
		// we have to wait for c++20 for string ends_with!!!
		if(basePath.size() > 0 && basePath.back() != '/')
			basePath += '/';
	}

	HandleRequestCB StaticFileManager::createHandler(string_view file) {
		return createHandler(getFile(file));
	}

	void StaticFileManager::timerCB() {
		if(loadsCounter*targetCacheHitRatio <= requestsCounter) {
			capacity -= capacity/8;
			if(capacity < minCapacity)
				capacity = minCapacity;
			//else fprintf(stderr, "static file cache capacity: %d\n", capacity);
			for(int i=0; i<maxPurgePerCycle; i++) {
				if(cache.size() <= capacity)
					break;
				pop();
			}
		}
		loadsCounter = requestsCounter = 0;
	}

	HandleRequestCB StaticFileManager::createHandler(LoadedStaticFile* file) {
		// the lambda retains a reference to file
		auto handler = [ref = FileRef(file)]
						(ConnectionHandler& ch) {
			auto* h = ch.allocateHandlerState<StaticFileHandler>(ch, ref.file);
			h->start();
		};
		return handler;
	}
	bool operator<(const string& a, string_view b) {
		string_view a1(a);
		return a1 < b;
	}
	LoadedStaticFile* StaticFileManager::getFile(string_view file) {
		// TODO: remove this cast to string once we switch to c++20
		// (to save an unnecessary heap allocation)
		string key(file);
		auto it = cache.find(key);
		if(it == cache.end()) {
			auto* f = loadFile(file);
			cache[key] = f;
			// we've added an item to the cache;
			// if we exceeded capacity then also remove an item from the cache.
			if(cache.size() > capacity)
				pop();

			// if in the last adjustment cycle we evicted more than half
			// of the cache, consider increasing capacity
			if(loadsCounter*2 > capacity) {
				// if we are getting a lot of cache misses then quickly ramp up capacity
				// without waiting for the timer callback to respond
				if(loadsCounter*targetCacheHitRatio > requestsCounter)
					capacity += capacity/8;
			}
			return f;
		}
		return (*it).second;
	}
	LoadedStaticFile* StaticFileManager::loadFile(string_view file) {
		string path = basePath;
		path.append(file);
		int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if(fd < 0)
			throw UNIXException(errno, path);

		struct stat st;
		if(fstat(fd, &st) < 0) {
			close(fd);
			throw UNIXException(errno, path);
		}
		if(!(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) {
			close(fd);
			throw CPollException("Requested path is not a file", EISDIR);
		}
		void* mapped = nullptr;
		if(st.st_size <= maxMmapSize) {
			mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
			if(mapped == nullptr) {
				close(fd);
				throw UNIXException(errno, string("mmap ") + path);
			}
		}

		// initialize fields to 0
		auto* f = new LoadedStaticFile {};
		f->manager = this;
		f->path = file;
		f->length = st.st_size;
		f->mapped = (uint8_t*) mapped;
		f->fd = fd;

		// only increment the counter after the file is loaded because
		// we don't want errors to count towards the load rate.
		loadsCounter++;
		return f;
	}
	// put the file on the to-free list
	void StaticFileManager::put(LoadedStaticFile* file) {
		file->prev = lastToFree;
		file->next = nullptr;
		if(lastToFree)
			lastToFree->next = file;
		lastToFree = file;
		if(firstToFree == nullptr)
			firstToFree = file;
	}
	// remove the file from the to-free list
	void StaticFileManager::retain(LoadedStaticFile* file) {
		if(file == firstToFree)
			firstToFree = file->next;
		if(file == lastToFree)
			lastToFree = file->prev;

		if(file->next == nullptr && file->prev == nullptr)
			return;
		if(file->prev)
			file->prev->next = file->next;
		if(file->next)
			file->next->prev = file->prev;
	}
	void StaticFileManager::pop() {
		LoadedStaticFile* toFree = firstToFree;
		if(toFree == nullptr)
			return;
		retain(toFree);

		// if it's on the to-free list it has refcount 0; we can delete it
		assert(toFree->refCount == 0);
		cache.erase(toFree->path);
		delete toFree;
	}
}
