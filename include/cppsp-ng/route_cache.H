#ifndef __INCLUDED_STATIC_HANDLER_H
#define __INCLUDED_STATIC_HANDLER_H

#include <cppsp-ng/cppsp.H>

using namespace std;
using namespace CP;

namespace cppsp {
	static constexpr int ROUTE_CACHE_MAX_PATH_LENGTH = 256;
	static constexpr int ROUTE_CACHE_ENTRIES_PER_HASH = 4;
	static constexpr int ROUTE_CACHE_DEFAULT_SIZE = 1024;

	struct RouteCacheKey {
		char path[ROUTE_CACHE_MAX_PATH_LENGTH];
	};
	struct RouteCacheEntry {
		RouteCacheKey keys[ROUTE_CACHE_ENTRIES_PER_HASH];
		HandleRequestCB handlers[ROUTE_CACHE_ENTRIES_PER_HASH];
		uint8_t nextToEvict;
	};
	class RouteCache {
	public:
		int size;
		int sizeMask;

		// size must be a power of 2
		RouteCache(int size = ROUTE_CACHE_DEFAULT_SIZE);
		~RouteCache();
		RouteCache(const RouteCache& other)=delete;
		RouteCache& operator=(const RouteCache& other) = delete;

		// find a handler in the cache for this path; returns nullptr if none found
		HandleRequestCB* find(string_view path);

		// add a handler to the cache
		void insert(string_view path, const HandleRequestCB& handler);

		// for diagnostics purposes only; list all entries in the cache
		void enumerate(function<void(string_view, HandleRequestCB&)>& cb);

		// internal functions
	public:
		RouteCacheEntry* table;
	};
}

#endif

