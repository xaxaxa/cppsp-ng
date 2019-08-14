#ifndef __INCLUDED_CPPSP_H
#define __INCLUDED_CPPSP_H

#include <cpoll-ng/string_builder.H>
#include <cpoll-ng/cpoll.H>
#include <functional>
#include <assert.h>

using namespace CP;

namespace std {
	class exception;
};
namespace cppsp {
	using std::string_view;
	using std::string;
	using std::vector;
	using std::pair;
	using std::exception;

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
		int headerCount() const { return headers.size(); }

		// returns the number of querystrings
		int queryStringCount() const { return queryStrings.size(); }

		// get a header by index
		pair<string_view, string_view> header(int i) const { return headers.at(i); }

		// get a querystring by index
		pair<string_view, string_view> queryString(int i) const { return queryStrings.at(i); }

		// get a header by name; name must be all lowercase
		string_view header(string_view name) const;

		// get a querystring by name; name is case sensitive
		string_view queryString(string_view name) const;
	};

	class Response {
	public:
		string_builder headersBuffer;
		string_builder buffer;
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

		/*template<class... P>
		int writeF(const char* fmt, P&&... p) {
			return (int) buffer.append_format(fmt, std::forward<P>(p)...);
		}*/
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
		static const int scratchAreaInitialSize = 256;
		Worker* worker;
		Socket socket;
		Request request;
		Response response;
		function<void(const exception&)> handleException;

		// called by Worker to start reading from a socket
		void start(int clientfd);

		// the ConnectionHandler provides a scratch area that you may allocate
		// one object from per request. The scratch area is recycled for every request.
		uint8_t* scratchArea(int minSize);

		template<class T, class... P>
		T* allocateHandlerState(P&&... p) {
			return new (scratchArea(sizeof(T))) T(std::forward<P>(p)...);
		}

		// prepares and returns the final http headers for transmission
		string_view composeHeaders();

		// called by the user application's request handler after
		// it has finished processing a request.
		// flushReponse controls whether data in "response" should be
		// written to the socket.
		void finish(bool flushReponse = true);

		// the function that handleException points to by default;
		// takes over the connection and writes an error message response.
		void defaultHandleException(const exception&);

		// called by the user application's request handler to abort a connection
		void abort();
	};
	
	static inline bool operator==(const std::string_view& a, std::nullptr_t b) {
		return a.empty();
	}
};

#endif
