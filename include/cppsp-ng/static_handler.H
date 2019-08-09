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
		int maxMmapSize = 1024*1024*4;
		string basePath;

		StaticFileManager(string basePath);
		HandleRequestCB createHandler(string_view file);


		// call this function once per second
		void timerCB();

		// internal function
	public:
		unordered_map<string, LoadedStaticFile*> cache;
		LoadedStaticFile* firstToFree;
		LoadedStaticFile* lastToFree;
		uint32_t requestsCounter = 0;
		uint32_t loadsCounter = 0;
		uint32_t maxSize = 128;

		LoadedStaticFile* getFile(string_view file);
		LoadedStaticFile* loadFile(string_view file);
		HandleRequestCB createHandler(LoadedStaticFile* file);

		// put the file on the to-free list
		void put(LoadedStaticFile* file);

		// remove the file from the to-free list
		void retain(LoadedStaticFile* file);
	};
}

#endif

