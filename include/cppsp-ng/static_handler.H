#ifndef __INCLUDED_STATIC_HANDLER_H
#define __INCLUDED_STATIC_HANDLER_H

#include <cppsp-ng/cppsp.H>
#include <functional>
#include <map>
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

		// basePath is prepended to all file paths
		StaticFileManager(string basePath);

		// retrieve a static file and create a handler for it.
		// the returned handler holds a reference to the file description
		// and prevents it from being removed from the cache.
		HandleRequestCB createHandler(string_view file);

		// call this function once per second
		void timerCB();

		// internal functions
	public:
		static const int targetCacheHitRatio = 20; // 20 hits per miss

		unordered_map<string, LoadedStaticFile*> cache;
		LoadedStaticFile* firstToFree = nullptr;
		LoadedStaticFile* lastToFree = nullptr;
		uint32_t requestsCounter = 0;
		uint32_t loadsCounter = 0;
		uint32_t capacity = 128;
		uint32_t minCapacity = 32;
		uint32_t maxPurgePerCycle = 128;

		LoadedStaticFile* getFile(string_view file);
		LoadedStaticFile* loadFile(string_view file);
		HandleRequestCB createHandler(LoadedStaticFile* file);

		// put the file on the to-free list
		void put(LoadedStaticFile* file);

		// remove the file from the to-free list
		void retain(LoadedStaticFile* file);

		// remove the least recently used item from the cache
		void pop();
	};
}

#endif

