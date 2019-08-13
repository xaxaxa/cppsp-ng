#include <cppsp-ng/cppsp.H>
#include <cppsp-ng/httpparser.H>
#include <cppsp-ng/stringutils.H>
#include <cppsp-ng/route_cache.H>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <time.h>

namespace cppsp {
	static inline int itoa64(int64_t i, char* b) {
		static char const digit[] = "0123456789";
		char* p = b;
		int l;
		p += (l=((i==0?0:int(log10(i))) + 1));
		*p = '\0';
		do { //Move back, inserting digits as u go
			*--p = digit[i % 10];
			i = i / 10;
		} while (i);
		return l;
	}

	string_view Request::header(string_view name) const {
		int n = headerCount();
		for(int i=0; i<n; i++) {
			if(HTTPParser::ci_equals(get<0>(headers[i]), name))
				return get<1>(headers[i]);
		}
		return string_view();
	}
	string_view Request::queryString(string_view name) const {
		int n = queryStringCount();
		for(int i=0; i<n; i++) {
			if(get<0>(queryStrings[i]) == name)
				return get<1>(queryStrings[i]);
		}
		return string_view();
	}
	void copyRequest(HTTPParser& parser, Request& request) {
		request.keepAlive = true;
		request.host = parser.host();
		request.path = parser.path();
		request.method = parser.verb();
		request.headers.resize(parser.headers.size());
		auto it2 = request.headers.begin();
		for(auto& h: parser.headers) {
			*it2 = parser.header(h);
			it2++;
		}
		if(HTTPParser::ci_equals(request.header("connection"), "close"))
			request.keepAlive = false;
	}

	const char* Response_defaultStatus = "200 OK";
	const char* Response_defaultContentType = "text/html; charset=UTF-8";

	// the number of bytes of free space to leave at the beginning of the
	// response headers buffer; this is used to fill in the status and
	// content type later.
	int Response_headersBufferInitialSpace = 160;

	void Response::reset() {
		status = Response_defaultStatus;
		contentType = Response_defaultContentType;
		headersBuffer.resize(Response_headersBufferInitialSpace);
		buffer.clear();
	}
	int Response::write(int i) {
		char tmp[12];
		int ret = snprintf(tmp, 12, "%d", i);
		buffer.append(tmp);
		return ret;
	}
	void Response::addHeader(string_view s) {
		headersBuffer.append(s);
	}
	void Response::addContentLengthHeader(int64_t length) {
		auto& h = headersBuffer;
		int oldLen = h.length();
		h.resize(h.length() + 64);
		char* s = h.data() + oldLen;
		memcpy(s, "Content-Length: ", 16);
		s += 16;
		s += itoa64(length, s);
		s[0] = '\r';
		s[1] = '\n';
		s[2] = '\r';
		s[3] = '\n';
		h.resize(s + 4 - h.data());
	}
	// fill in fixed headers
	string_view Response::composeHeaders(int64_t contentLength, string_view date) {
		addHeader(date);
		addContentLengthHeader(contentLength);
		if(status == Response_defaultStatus
			&& contentType == Response_defaultContentType) {
			int sp, len;
			if(keepAlive) {
				len = 17 + 24 + 40;
				sp = Response_headersBufferInitialSpace - len;
				memcpy(headersBuffer.data() + sp,
					"HTTP/1.1 200 OK\r\n"
					"Connection: keep-alive\r\n"
					"Content-Type: text/html; charset=UTF-8\r\n", len);
			} else {
				len = 17 + 19 + 40;
				sp = Response_headersBufferInitialSpace - len;
				memcpy(headersBuffer.data() + sp,
					"HTTP/1.1 200 OK\r\n"
					"Connection: close\r\n"
					"Content-Type: text/html; charset=UTF-8\r\n", len);
			}
			return {headersBuffer.data() + sp, headersBuffer.length() - sp};
		}
		int statusLen = strlen(status);
		int ctLen = strlen(contentType);

		int totalLen = 9 + statusLen + 2
					+ (keepAlive ? 24 : 19)
					+ 14 + ctLen + 2;
		if(totalLen > Response_headersBufferInitialSpace)
			return string_view();

		int sp = Response_headersBufferInitialSpace - totalLen;
		char* s = headersBuffer.data() + sp;
		memcpy(s, "HTTP/1.1 ", 9);											s += 9;
		memcpy(s, status, statusLen);										s += statusLen;
		if(keepAlive) {
			memcpy(s, "\r\nConnection: keep-alive\r\nContent-Type: ", 40);	s += 40;
		} else {
			memcpy(s, "\r\nConnection: close\r\nContent-Type: ", 35);		s += 35;
		}
		memcpy(s, contentType, ctLen);										s += ctLen;
		s[0] = '\r';
		s[1] = '\n';
		return {headersBuffer.data() + sp, headersBuffer.length() - sp};
	}


	template<class T>
	class ObjectPool {
	public:
		vector<T*> pool;
		T* get() {
			if(pool.empty())
				return new T();
			T* ret = pool.back();
			pool.pop_back();
			return ret;
		}
		void put(T* obj) {
			pool.push_back(obj);
		}
	};
	
	class ConnectionHandlerInternal: public ConnectionHandler {
	public:
		HTTPParser parser;
		string stringPool;
		vector<tuple<int,int,int,int> > qsIndices;
		iovec iov[2];
		uint8_t* scratch = nullptr;
		int scratchSize = 0;

		uint8_t* scratchArea(int minSize) {
			if(minSize > scratchSize) {
				if(scratch != nullptr)
					free(scratch);
				int sz = scratchSize;
				if(sz < scratchAreaInitialSize)
					sz = scratchAreaInitialSize;
				while(sz < minSize)
					sz *= 2;
				scratch = (uint8_t*) malloc(sz);
				if(!scratch) {
					scratchSize = 0;
					throw bad_alloc();
				}
				scratchSize = sz;
			}
			return scratch;
		}
		void start(int clientfd) {
			//fprintf(stderr, "NEW CONNECTION\n");
			parser.reset();
			socket.fd = clientfd;
			worker->epoll.add(socket);
			startRead();
		}
		void startRead() {
			//fprintf(stderr, "startRead\n");
			auto bufView = parser.beginAddData();
			char* buffer = get<0>(bufView);
			int len = get<1>(bufView);
			socket.recv(buffer, len, 0, [this](int r) {
				readCB(r);
			});
		}
		void readCB(int r) {
			//fprintf(stderr, "readCB %d\n", r);
			if(r <= 0) {
				stop();
				return;
			}
			//fprintf(stderr, "bufferBegin %d, bufferProcessed %d\n", parser.bufferBegin, parser.bufferProcessed);
			parser.endAddData(r);
			//write(1, parser.buffer + parser.bufferBegin, parser.bufferEnd - parser.bufferBegin);
			if(parser.readRequest()) {
				processRequest();
			} else {
				startRead();
			}
		}
		void parseQueryString() {
			request.queryStrings.clear();
			string_view path = request.path;

			int que = path.find('?');
			if(que == string_view::npos)
				return;

			const char* qsStart = path.data() + que + 1;
			int qsLen = path.length() - que - 1;
			cppsp::parseQueryString(qsStart, qsLen, stringPool, qsIndices);

			request.queryStrings.resize(qsIndices.size());
			auto it2 = request.queryStrings.begin();
			const char* spStart = stringPool.data();
			for(auto& item: qsIndices) {
				int nS = get<0>(item), nE = get<1>(item);
				int vS = get<2>(item), vE = get<3>(item);
				*it2 = {string_view(spStart + nS, nE - nS),
						string_view(spStart + vS, vE - vS)};
				it2++;
			}
			qsIndices.clear();
			request.path = path.substr(0, que);
		}
		void processRequest() {
			response.reset();
			response.keepAlive = request.keepAlive;
			stringPool.clear();
			if(parser.malformed) {
				request.keepAlive = false;
				response.buffer += "Malformed request";
				response.status = "400 Bad Request";
				response.contentType = "text/plain";
				finish(true);
				return;
			}
			copyRequest(parser, request);
			parseQueryString();
			//fprintf(stderr, "processRequest\n");
			//string path(parser.path());
			//printf("%s\n", path.c_str());

			try {
				int len1 = request.host.length();
				int len2 = request.path.length();
				char k[len1 + len2 + 1];
				memcpy(k, request.host.data(), len1);
				memcpy(k+len1+1, request.path.data(), len2);
				k[len1] = '#';
				string_view key(k, len1 + len2 + 1);

				auto* cachedHandler = worker->routeCache->find(key);
				if(cachedHandler != nullptr) {
					(*cachedHandler)(*this);
					return;
				}
				if(worker->router != nullptr) {
					auto handler = worker->router(request.host, request.path);
					worker->routeCache->insert(key, handler);
					handler(*this);
				} else {
					worker->handler(*this);
				}
			} catch(exception& ex) {
				handleException(ex);
			}
		}
		
		void finish(bool flushReponse) {
			if(flushReponse) {
				auto headers = response.composeHeaders(response.buffer.length(), worker->date());
				if(headers == nullptr) {
					runtime_error ex("Response status or content type too large");
					handleException(ex);
					return;
				}
				iov[0].iov_base = (void*) headers.data();
				iov[0].iov_len = headers.length();
				iov[1].iov_base = response.buffer.data();
				iov[1].iov_len = response.buffer.length();
				socket.writevAll(iov, 2, [this](int r) {
					if(r <= 0)
						abort();
					else requestCompleted();
				});
			} else
				requestCompleted();
		}
		void handleException(exception& ex) {
			response.buffer.clear();
			response.status = "500 Server Error";
			response.contentType = "text/html";
			response.buffer.append("<html><head><title>Server error</title></head>\n");
			response.buffer.append("<body><h1 style=\"color: #aa1111\">Server error</h1><hr />"
					"<h2 style=\"color: #444\">");
			htmlEscape(ex.what(), response.buffer);
			response.buffer.append("</h2>");
			response.buffer.append("</body></html>");
			finish(true);
		}
		void abort() {
			stop();
		}
		void requestCompleted() {
			if(!request.keepAlive) {
				stop();
				return;
			}
			parser.clearRequest();
			if(parser.readRequest()) {
				processRequest();
			} else {
				startRead();
			}
		}
		void stop() {
			socket.shutdown(SHUT_WR);
			worker->epoll.remove(socket);
			socket.close();
			worker->connectionClosedCB(this);
		}
	};

	void ConnectionHandler::start(int clientfd) {
		ConnectionHandlerInternal* th = (ConnectionHandlerInternal*) this;
		th->start(clientfd);
	}
	uint8_t* ConnectionHandler::scratchArea(int minSize) {
		ConnectionHandlerInternal* th = (ConnectionHandlerInternal*) this;
		return th->scratchArea(minSize);
	}
	void ConnectionHandler::finish(bool flushReponse) {
		ConnectionHandlerInternal* th = (ConnectionHandlerInternal*) this;
		th->finish(flushReponse);
	}
	void ConnectionHandler::abort() {
		ConnectionHandlerInternal* th = (ConnectionHandlerInternal*) this;
		th->abort();
	}

	typedef ObjectPool<ConnectionHandlerInternal> HandlerPool;
	Worker::Worker() {
		handlerPool = new HandlerPool();
		routeCache = new RouteCache();
		timerCB();
	}
	Worker::~Worker() {
		HandlerPool* hp = (HandlerPool*) handlerPool;
		delete hp;
		delete routeCache;
	}
	void Worker::addListenSocket(Socket& sock) {
		epoll.add(sock);
		sock.repeatAccept([this](int r) {
			HandlerPool* hp = (HandlerPool*) handlerPool;
			if(r < 0) {
				fprintf(stderr, "socket accept() error: %s\n", strerror(errno));
				exit(1);
				return;
			}
			auto* h = hp->get();
			h->worker = this;
			h->start(r);
		});
	}
	void Worker::loop() {
		epoll.loop();
	}
	void Worker::timerCB() {
		currDate.clear();
		currDate += "Date: ";
		
		//currDate.resize(50);
		time_t t;
		tm tm1;
		time(&t);
		gmtime_r(&t, &tm1);
		/*int len = rfctime(tm1, currDate.data() + 6);
		currDate.resize(len + 6);*/

		rfctime2(tm1, currDate);
		
		currDate += "\r\n";
	}

	void Worker::connectionClosedCB(ConnectionHandler* _ch) {
		auto* ch = (ConnectionHandlerInternal*) _ch;
		HandlerPool* hp = (HandlerPool*) handlerPool;
		hp->put(ch);
		//delete ch;
	}
}
