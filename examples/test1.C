#include <cpoll-ng/cpoll.H>
#include <cppsp-ng/cppsp.H>
#include <cppsp-ng/static_handler.H>
#include <cppsp-ng/stringutils.H>
#include <iostream>
#include <signal.h>
#include <assert.h>

using namespace CP;
using namespace cppsp;


struct MyHandler {
	ConnectionHandler& ch;
	MyHandler(ConnectionHandler& ch): ch(ch) {}
	void handleHome() {
		ch.response.write("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
		finish(true);
	}
	void handlePing() {
		ch.response.write("PONG");
		ch.response.addHeader("Server: fuck-you\r\n");
		finish(true);
	}
	void handleQs() {
		string& out = ch.response.buffer;
		out += "<pre>querystrings:\n";
		int qsCount = ch.request.queryStringCount();
		for(int i=0; i<qsCount; i++) {
			string_view name, value;
			tie(name, value) = ch.request.queryString(i);
			htmlEscape(name, out);
			out += ": ";
			htmlEscape(value, out);
			out += '\n';
		}
		out += "</pre>";
		finish(true);
	}
	void finish(bool flush) {
		this->~MyHandler();
		ch.finish(flush);
	}
};

// given a type and a member function, create a handler that
// will instantiate the type and call the member function.
template<class T, void (T::*FUNC)()>
HandleRequestCB createMyHandler() {
	return [](ConnectionHandler& ch) {
		auto* tmp = ch.allocateHandlerState<T>(ch);
		(tmp->*FUNC)();
	};
}


void runWorker(Worker& worker, Socket& srvsock) {
	StaticFileManager sfm(".");

	// request router; given a http path return a HandleRequestCB
	auto router = [&](string_view host, string_view path) {
		string tmp(path);
		printf("%s\n", tmp.c_str());
		if(path.compare("/ping") == 0)
			return createMyHandler<MyHandler, &MyHandler::handlePing>();
		if(path.compare("/100") == 0)
			return createMyHandler<MyHandler, &MyHandler::handleHome>();
		if(path.compare("/qs") == 0)
			return createMyHandler<MyHandler, &MyHandler::handleQs>();
		return sfm.createHandler(path);
	};
	worker.router = router;
	worker.addListenSocket(srvsock);

	Timer timer((uint64_t) 1000);
	timer.setCallback([&](int r) {
		worker.timerCB();
		sfm.timerCB();
	});
	worker.epoll.add(timer);
	worker.loop();
}

int main(int argc, char** argv)
{
	if(argc<3) {
		cerr << "usage: " << argv[0] << " bind_host bind_port" << endl;
		return 1;
	}
	Socket srvsock;
	srvsock.bind(argv[1], argv[2]);
	srvsock.listen();

	int nThreads = 8;

	for(int i=1; i<nThreads; i++) {
		createThread([argv]() {
			Socket srvsock;
			srvsock.bind(argv[1], argv[2]);
			srvsock.listen();
			Worker worker;
			runWorker(worker, srvsock);
		});
	}
	Worker worker;
	runWorker(worker, srvsock);
}
