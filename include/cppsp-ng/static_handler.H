#ifndef __INCLUDED_STATIC_HANDLER_H
#define __INCLUDED_STATIC_HANDLER_H

#include <cppsp-ng/cppsp.H>
#include <functional>
#include <unordered_map>
#include <assert.h>

using namespace std;
using namespace CP;

namespace cppsp {
	class LoadedStaticFile;
	
	class StaticFileManager {
	public:
		// files below this size will be mmap()ed, and above this size
		// sendfile(2) will be used.
		int maxMmapSize = 1024*1024*4;
		string basePath;
		unordered_map<string, string> mimeDB;
		string defaultMime = "text/plain";

		// basePath is prepended to all file paths
		StaticFileManager(string basePath);

		// retrieve a static file and create a handler for it.
		// the returned handler holds a reference to the file description
		// and prevents it from being removed from the cache.
		HandleRequestCB createHandler(string_view file);

		// call this function once per second
		void timerCB();

		// this function is used to determine the mime type to serve
		// and its return value is cached.
		function<string(string_view file)> mimeType;

		// internal functions
	public:
		static const int targetCacheHitRatio = 50; // 50 hits per miss

		// all LoadedStaticFile instances should be in this map regardless
		// of whether it's loaded; any LoadedStaticFile that is both
		// unloaded and unreferenced are removed immediately.
		unordered_map<string, LoadedStaticFile*> cache;

		// only loaded files should be put on the to-free list
		LoadedStaticFile* firstToFree = nullptr;
		LoadedStaticFile* lastToFree = nullptr;
		uint32_t requestsCounter = 0;
		uint32_t loadsCounter = 0;
		uint32_t capacity = 128;
		uint32_t minCapacity = 32;
		uint32_t maxCapacity = 1024;
		uint32_t maxPurgePerCycle = 128;
		uint32_t nLoaded = 0;

		LoadedStaticFile* getFile(string_view file);
		LoadedStaticFile* addFile(string_view file);
		void removeFile(string_view file);
		HandleRequestCB createHandler(LoadedStaticFile* file);

		// map the file and put it on the to-free list
		void load(LoadedStaticFile* file);

		// same as load(), but also evict entries if the cache exceeds capacity
		void loadAndEvict(LoadedStaticFile* file);

		// unmap or close the file and remove from the to-free list
		void unload(LoadedStaticFile* file);

		void doLoad(LoadedStaticFile* file);
		void doUnload(LoadedStaticFile* file);


		// unload the least recently used file
		void pop();
	};
	void loadMimeDB(unordered_map<string, string>& out);
}

#endif

