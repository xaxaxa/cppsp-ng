#include <cppsp-ng/static_handler.H>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>

using namespace std;
using namespace CP;

namespace cppsp {
	struct LoadedStaticFile {
		// position in the to-free list
		LoadedStaticFile* next = nullptr;
		LoadedStaticFile* prev = nullptr;
		StaticFileManager* manager = nullptr;
		string path;
		string mimeType;
		unique_ptr<exception> lastException = nullptr;
		uint8_t* mapped = nullptr;
		timespec lastModified = timespec();
		timespec lastCheck = timespec();
		int fd = -1;
		int length = 0;
		int refCount = 0;
		bool loaded() {
			return (mapped != nullptr) || fd >= 0;
		}
	};
	static int tsCompare(struct timespec time1, struct timespec time2) {
		if (time1.tv_sec < time2.tv_sec) return (-1); /* Less than. */
		else if (time1.tv_sec > time2.tv_sec) return (1); /* Greater than. */
		else if (time1.tv_nsec < time2.tv_nsec) return (-1); /* Less than. */
		else if (time1.tv_nsec > time2.tv_nsec) return (1); /* Greater than. */
		else return (0); /* Equal. */
	}
	struct FileRef {
		LoadedStaticFile* file;

		// if refcount becomes 0, put the file on the to-free list;
		// if refcount becomes nonzero, remove the file from the to-free list.

		FileRef(LoadedStaticFile* file):file(file) {
			file->refCount++;
		}
		FileRef(const FileRef& other): file(other.file) {
			file->refCount++;
		}
		FileRef& operator=(const FileRef& other) = delete;
		~FileRef() {
			file->refCount--;
			// if we are unreferenced and unloaded (implies not in to-free list)
			// get rid of the map entry.
			if(file->refCount == 0 && !file->loaded())
				file->manager->removeFile(file->path);
		}
		LoadedStaticFile& operator*() const {
			return *file;
		}
		LoadedStaticFile* operator->() const {
			return file;
		}
	};
	struct StaticFileHandler {
		ConnectionHandler& ch;
		FileRef file;
		union {
			iovec iov[2];
			int64_t sendFileOffset;
		};
		StaticFileHandler(ConnectionHandler& ch, LoadedStaticFile* file)
			: ch(ch), file(file) { }
		void start() {
			auto& response = ch.response;
			file->manager->requestsCounter++;

			response.contentType = file->mimeType.c_str();
			string_view headers = response.composeHeaders(file->length, ch.worker->date());

			// if we have the file mmap'd, we can use writev to send the
			// headers and contents at once; otherwise use sendfile()
			if(file->mapped) {
				doWriteVFile(headers);
			} else {
				ch.socket.sendAll(headers.data(), headers.length(), MSG_MORE,
								[this](int r) {
					if(r <= 0) {
						abort();
						return;
					}
					sendFileOffset = 0;
					doSendFile();
				});
			}
		}
		void doWriteVFile(string_view headers) {
			iov[0].iov_base = (void*) headers.data();
			iov[0].iov_len = headers.length();
			iov[1].iov_base = file->mapped;
			iov[1].iov_len = file->length;
			ch.socket.writevAll(iov, 2, [this](int r) {
				uint32_t totalLen = iov[0].iov_len + iov[1].iov_len;
				this->~StaticFileHandler();
				if(r <= 0 || r < totalLen) {
					fprintf(stderr, "writev failed: %d %s\n", r, strerror(errno));
					ch.abort();
				}
				else ch.finish(false);
			});
		}
		void doSendFile() {
			ch.socket.sendFileFrom(file->fd, sendFileOffset, 1024*1024, [this](int r) {
				if(r <= 0 || (sendFileOffset + r) >= file->length) {
					finish();
				} else {
					sendFileOffset += r;
					doSendFile();
				}
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

	StaticFileManager::StaticFileManager(string bp): basePath(bp) {
		// we have to wait for c++20 for string ends_with!!!
		if(basePath.size() > 0 && basePath.back() != '/')
			basePath += '/';
		loadMimeDB(mimeDB);
		mimeType = [this](string_view file) {
			int dot = file.rfind('.');
			if(dot == string_view::npos)
				return defaultMime;
			string_view ext = file.substr(dot + 1);

			// TODO: remove cast to string after switching to c++20
			string ext1(ext);
			auto it = mimeDB.find(ext1);
			if(it == mimeDB.end())
				return defaultMime;
			return (*it).second;
		};
		timerCB();
	}

	HandleRequestCB StaticFileManager::createHandler(string_view file) {
		return createHandler(getFile(file));
	}

	void StaticFileManager::timerCB() {
		clock_gettime(CLOCK_MONOTONIC, &currTime);
		if(loadsCounter*targetCacheHitRatio <= requestsCounter) {
			capacity -= capacity/8;
			if(capacity < minCapacity)
				capacity = minCapacity;
			//else fprintf(stderr, "static file cache capacity: %d\n", capacity);
			for(int i=0; i<maxPurgePerCycle; i++) {
				if(nLoaded <= capacity)
					break;
				pop();
			}
		}
		//fprintf(stderr, "static file cache capacity: %d\n", capacity);
		loadsCounter = requestsCounter = 0;
	}

	HandleRequestCB StaticFileManager::createHandler(LoadedStaticFile* file) {
		// the lambda retains a reference to file
		auto handler = [ref = FileRef(file)]
						(ConnectionHandler& ch) {
			ref->manager->reloadIfStale(ref.file);
			if(!ref->loaded()) {
				if(ref->lastException == nullptr) ch.abort();
				else ch.handleException(*ref->lastException);
				return;
			}
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
		if(it == cache.end())
			return addFile(file);
		return (*it).second;
	}
	LoadedStaticFile* StaticFileManager::addFile(string_view file) {
		// TODO: remove this cast to string once we switch to c++20
		// (to save an unnecessary heap allocation)
		string key(file);
		// initialize fields to 0
		auto* f = new LoadedStaticFile {};
		cache[key] = f;
		f->manager = this;
		f->path = file;
		f->fd = -1;
		//load(f);
		return f;
	}
	void StaticFileManager::removeFile(string_view file) {
		// TODO: remove this cast to string once we switch to c++20
		// (to save an unnecessary heap allocation)
		string key(file);
		delete cache[key];
		cache.erase(key);
	}

	void StaticFileManager::loadAndEvict(LoadedStaticFile* file) {
		load(file);
		// we've added an item to the cache;
		// if we exceeded capacity then also remove an item from the cache.
		if(nLoaded > capacity)
			pop();

		// if in the last adjustment cycle we evicted more than half
		// of the cache, consider increasing capacity
		if(loadsCounter*2 > capacity) {
			// if we are getting a lot of cache misses then quickly ramp up capacity
			// without waiting for the timer callback to respond
			if(loadsCounter*targetCacheHitRatio > requestsCounter) {
				//fprintf(stderr, "%d loads, %d requests\n", loadsCounter, requestsCounter);
				capacity += capacity/8;
			}
			if(capacity > maxCapacity)
				capacity = maxCapacity;
		}
	}
	void StaticFileManager::load(LoadedStaticFile* file) {
		file->lastCheck = currTime;
		file->lastException.reset(doLoad(file));
		if(file->lastException) return;
		// put the file on the to-free list
		nLoaded++;
		file->prev = lastToFree;
		file->next = nullptr;
		if(lastToFree)
			lastToFree->next = file;
		lastToFree = file;
		if(firstToFree == nullptr)
			firstToFree = file;
	}
	void StaticFileManager::unload(LoadedStaticFile* file) {
		doUnload(file);
		// remove the file from the to-free list
		nLoaded--;

		if(file->next == nullptr)
			assert(file == lastToFree);
		if(file->prev == nullptr)
			assert(file == firstToFree);

		if(file == firstToFree)
			firstToFree = file->next;
		if(file == lastToFree)
			lastToFree = file->prev;

		if(file->prev)
			file->prev->next = file->next;
		if(file->next)
			file->next->prev = file->prev;
		
		if(file->refCount == 0) {
			cache.erase(file->path);
			delete file;
		}
	}
	exception* StaticFileManager::doLoad(LoadedStaticFile* f) {
		string path = basePath;
		path.append(f->path);

		int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if(fd < 0) {
			return new UNIXException(errno, path);
		}

		struct stat st;
		if(fstat(fd, &st) < 0) {
			close(fd);
			return new UNIXException(errno, path);
		}
		if(!(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) {
			close(fd);
			return new CPollException("Requested path is not a file", EISDIR);
		}
		void* mapped = nullptr;
		if(st.st_size <= maxMmapSize) {
			mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
			close(fd);
			if(mapped == nullptr) {
				return new UNIXException(errno, string("mmap ") + path);
			}
			fd = -1;
		}

		f->length = st.st_size;
		f->mapped = (uint8_t*) mapped;
		f->fd = fd;
		f->mimeType = mimeType(f->path);
		f->lastModified = st.st_mtim;

		// only increment the counter after the file is loaded because
		// we don't want errors to count towards the load rate.
		loadsCounter++;
		return nullptr;
	}
	void StaticFileManager::doUnload(LoadedStaticFile* f) {
		if(f->mapped)
			munmap(f->mapped, f->length);
		if(f->fd >= 0)
			close(f->fd);
		f->mapped = nullptr;
		f->fd = -1;
	}
	// assumes f is loaded
	void StaticFileManager::reloadIfStale(LoadedStaticFile* f) {
		timespec tmp1 = currTime;
		tmp1.tv_sec -= 2;
		// if we already checked within the last 2 seconds then return
		if (tsCompare(f->lastCheck, tmp1) > 0)
			return;

		if(!f->loaded()) {
			load(f);
			return;
		}

		// check if the file needs to be reloaded
		f->lastCheck = currTime;
		string path = basePath;
		path.append(f->path);
		struct stat st;
		if(stat(path.c_str(), &st) < 0) {
			f->lastException.reset(new UNIXException(errno, path));
			unload(f);
			return;
		}

		// if last-modified changed or size changed, reload
		if((tsCompare(f->lastModified, st.st_mtim) != 0)
			|| (f->length != st.st_size)) {
			unload(f);
			load(f);
		}
	}
	void StaticFileManager::pop() {
		LoadedStaticFile* toFree = firstToFree;
		if(toFree == nullptr)
			return;
		unload(toFree);
	}



	void loadMimeDB(unordered_map<string, string>& out) {
		const char* mimePath = "/usr/share/mime/globs";
		FILE* file = fopen(mimePath, "rb");
		if(file == nullptr) {
			fprintf(stderr, "load mime db %s failed: %s\n", mimePath, strerror(errno));
			const char* mimes[] = {
				"htm", "text/html",
				"html", "text/html",
				"txt", "text/plain",
				"css", "text/css",
				"js", "application/javascript",
				"jpg", "image/jpeg",
				"jpeg", "image/jpeg",
				"png", "image/png",
				"gif", "image/gif",
				"webp", "image/webp",
				"cpp", "text/x-c++src",
				"cxx", "text/x-c++src",
				"cc", "text/x-c++src",
				"C", "text/x-c++src",
				"c", "text/x-csrc",
				"h", "text/x-csrc",
				"H", "text/x-c++src",
				"hpp", "text/x-c++src",
				"hxx", "text/x-c++src"};
			for(int i=0; i<(int)(sizeof(mimes)/sizeof(*mimes)); i+=2) {
				out.insert({mimes[i], mimes[i+1]});
			}
			return;
		}
		while (true) {
			char* line_ = nullptr;
			size_t n = 0;
			if(getline(&line_, &n, file) <= 0) {
				if(line_ != nullptr) free(line_);
				break;
			}
			string_view line(line_);
			line = line.substr(0, line.length() - 1);

			int i = line.find(':');
			if (i == string_view::npos)
				goto cont;

			{
				string_view ext = line.substr(i + 1);
				if (ext.length() < 3)
					goto cont;
				if (!(ext[0] == '*' && ext[1] == '.'))
					goto cont;
				ext = ext.substr(2);
				out.insert( { string(ext), string(line.substr(0, i)) });
			}
		cont:
			free(line_);
		}
	}
}
