#ifndef __INCLUDED_CPPSP_H
#define __INCLUDED_CPPSP_H

#include <cpoll-ng/cpoll.H>
#include <functional>
#include <assert.h>

using namespace CP;

namespace cppsp {
	using std::string_view;
	using std::string;
	using std::vector;
	using std::pair;

	static constexpr int HANDLER_MEM_POOL = 1024;

	class ConnectionHandler;
	class RouteCache;

	// application callbacks

	// called by a worker to process one http request; after the user
	// is done with the request it should call ch.finish().
	typedef function<void(ConnectionHandler& ch)> HandleRequestCB;
	typedef function<HandleRequestCB(string_view host, string_view path)> RouteRequestCB;

	class Request {
	public:
		string_view method;
		string_view host;
		string_view path;
		vector<pair<string_view, string_view> > headers;
		vector<pair<string_view, string_view> > queryStrings;
		bool keepAlive;

		// returns the number of request headers
		int headerCount() { return headers.size(); }

		// returns the number of querystrings
		int queryStringCount() { return queryStrings.size(); }

		// get a header by index
		pair<string_view, string_view> header(int i) { return headers.at(i); }

		// get a querystring by index
		pair<string_view, string_view> queryString(int i) { return queryStrings.at(i); }

		// get a header by name; name must be all lowercase
		string_view header(string_view name);

		// get a querystring by name; name is case sensitive
		string_view queryString(string_view name);
	};

	class Response {
	public:
		string headersBuffer;
		string buffer;
		const char* status;
		const char* contentType;
		bool keepAlive;

		void reset();
		void write(string_view s) { buffer.append(s); }
		void write(const char* s) { buffer.append(s); }
		int write(int i);

		// s should include crlf
		void addHeader(string_view s);

		// adds a content-length response header and trailing \r\n\r\n
		void addContentLengthHeader(int64_t length);

		// prepares and returns the final http headers for transmission;
		// returns nullptr if the pre-headers (status and content-type)
		// do not fit in the preallocated space.
		string_view composeHeaders(int64_t contentLength, string_view date);

		template<class... P>
		int writeF(const char* fmt, P&&... p) {
			buffer.reserve(buffer.size() + 32);
			int space = buffer.capacity() - buffer.size();
			int len = snprintf(buffer.data() + buffer.size(), space, fmt, std::forward<P>(p)...);
			// len excludes the null byte, but space includes it
			if(len >= space) {
				buffer.reserve(buffer.size() + len + 1);
				return snprintf(buffer.data() + buffer.size(), len + 1, fmt, std::forward<P>(p)...);
			}
			return len;
		}
	};

	class Worker {
	public:
		EPoll epoll;

		// if router is null, handler is used
		RouteRequestCB router;
		HandleRequestCB handler;

		Worker();
		~Worker();
		void addListenSocket(Socket& sock);
		void loop();

		// this function should be called once per second
		void timerCB();

		// returns the Date: http header
		string_view date() { return currDate; }
		void connectionClosedCB(ConnectionHandler* ch);

		// internal functions
	public:
		void* handlerPool;
		RouteCache* routeCache;
		string currDate;
	};

	// this implementation is tied to Worker. Do not instantiate directly.
	class ConnectionHandler {
	public:
		static constexpr int scratchAreaSize = 256;
		Worker* worker;
		Socket socket;
		Request request;
		Response response;
		uint8_t scratchArea[scratchAreaSize];

		// called by Worker to start reading from a socket
		void start(int clientfd);

		// the ConnectionHandler provides a scratch area that you may allocate
		// one object from per request. The scratch area is recycled for every request.
		template<class T, class... P>
		T* allocateHandlerState(P&&... p) {
			assert(sizeof(T) <= scratchAreaSize);
			return new (scratchArea) T(forward<P>(p)...);
		}

		// prepares and returns the final http headers for transmission
		string_view composeHeaders();

		// called by the user application's request handler after
		// it has finished processing a request.
		// flushReponse controls whether data in "response" should be
		// written to the socket.
		void finish(bool flushReponse = true);

		// called by the user application's request handler to abort a connection
		void abort();
	};
};

#endif
