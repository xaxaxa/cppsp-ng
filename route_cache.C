#include <cppsp-ng/route_cache.H>


using namespace std;
using namespace CP;

namespace cppsp {
	RouteCache::RouteCache(int size): size(size) {
		sizeMask = size - 1;
		assert((size | sizeMask) == (size + sizeMask));
		table = new RouteCacheEntry[size];
		for(int i=0; i<size; i++) {
			for(int j=0; j<ROUTE_CACHE_ENTRIES_PER_HASH; j++)
				table[i].keys[j].path[0] = 0;
			table[i].nextToEvict = 0;
		}
	}
	RouteCache::~RouteCache() {
		delete[] table;
	}

	HandleRequestCB* RouteCache::find(string_view path) {
		int h = sdbm((uint8_t*) path.data(), path.length()) & sizeMask;
		auto& entry = table[h];
		for(int i=0; i<ROUTE_CACHE_ENTRIES_PER_HASH; i++) {
			string_view p(entry.keys[i].path);
			if(p == path)
				return &entry.handlers[i];
		}
		return nullptr;
	}
	void RouteCache::insert(string_view path, const HandleRequestCB& handler) {
		if(path.length() + 1 > ROUTE_CACHE_MAX_PATH_LENGTH)
			return;
		int h = sdbm((uint8_t*) path.data(), path.length()) & sizeMask;
		auto& entry = table[h];
		auto& key = entry.keys[entry.nextToEvict];
		auto& val = entry.handlers[entry.nextToEvict];
		memcpy(key.path, path.data(), path.length());
		key.path[path.length()] = 0;
		val = handler;
		entry.nextToEvict = (entry.nextToEvict+1) % ROUTE_CACHE_ENTRIES_PER_HASH;
	}
	void RouteCache::enumerate(function<void(string_view, HandleRequestCB&)>& cb) {
		for(int i=0; i<size; i++) {
			auto& entry = table[i];
			for(int j=0; j<ROUTE_CACHE_ENTRIES_PER_HASH; j++) {
				if(entry.keys[j].path[0] != 0)
					cb(entry.keys[j].path, entry.handlers[j]);
			}
		}
	}
}
