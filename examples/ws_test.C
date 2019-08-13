#include <cpoll-ng/cpoll.H>
#include <cppsp-ng/cppsp.H>
#include <cppsp-ng/static_handler.H>
#include <cppsp-ng/stringutils.H>
#include <cppsp-ng/websocket.H>
#include <iostream>
#include <signal.h>
#include <assert.h>

using namespace CP;
using namespace cppsp;

extern const char* homeHTML;

struct MyHandler {
	ConnectionHandler& ch;
	WebSocketParser wsp;
	FrameWriter wsw;

	MyHandler(ConnectionHandler& ch): ch(ch) {}
	void handle100() {
		ch.response.write("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
		finish(true);
	}
	void handleHome() {
		if(ws_iswebsocket(ch.request)) {
			handleWebsocket();
			return;
		}
		ch.response.write(homeHTML);
		finish(true);
	}
	void handleWebsocket() {
		ws_init(ch, [this](int r) {
			if(r <= 0) {
				abort();
				return;
			}
			wsStart();
		});
	}
	void wsStart() {
		wsw.streamWriteAll = [this](const void* buf, int len, const Callback& cb) {
			ch.socket.writeAll(buf, len, cb);
		};
		wsRead();
	}
	void wsRead() {
		auto buf = wsp.beginAddData();
		ch.socket.read(get<0>(buf), get<1>(buf), [this](int r) {
			if(r <= 0) {
				abort();
				return;
			}
			wsp.endAddData(r);
			WebSocketParser::WSFrame f;
			while(wsp.process(f)) {
				handleFrame(f);
			}
			wsRead();
		});
	}
	void handleFrame(WebSocketParser::WSFrame& f) {
		//printf("websocket frame: opcode=%i fin=%i datalen=%i data:\n%s\n",f.opcode,f.fin?1:0,f.data.length(),f.data.toSTDString().c_str());
		auto buf = wsw.beginAppend(f.data.length());
		memcpy(buf, f.data.data(), f.data.length());
		wsw.endAppend(f.opcode);
		wsw.flush();
	}
	void finish(bool flush) {
		this->~MyHandler();
		ch.finish(flush);
	}
	void abort() {
		this->~MyHandler();
		ch.abort();
	}
};

// given a type and a member function, create a handler that
// will instantiate the type and call the member function.
template<class T, void (T::*FUNC)()>
HandleRequestCB createMyHandler() {
	return [](ConnectionHandler& ch) {
		// allocateHandlerState will emplace an object into the connection
		// handler scratch space; the memory must NOT be freed.
		// Your handler must call its own destructor when it is done
		// with the request and has passed control back to ConnectionHandler
		// (via finish() or abort()).
		auto* tmp = ch.allocateHandlerState<T>(ch);
		(tmp->*FUNC)();
	};
}


void runWorker(Worker& worker, Socket& srvsock) {
	StaticFileManager sfm(".");

	// request router; given a http path return a HandleRequestCB
	auto router = [&](string_view host, string_view path) {
		string tmp(host);
		tmp += ": ";
		tmp += path;
		printf("%s\n", tmp.c_str());
		if(path.compare("/") == 0)
			return createMyHandler<MyHandler, &MyHandler::handleHome>();
		if(path.compare("/100") == 0)
			return createMyHandler<MyHandler, &MyHandler::handle100>();
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

	int nThreads = 1;

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

const char* homeHTML = R"EOF(
<!DOCTYPE html>
<html>
	<head>
		<meta charset="utf-8" />
		<title>WebSocket Test</title>
		<script language="javascript" type="text/javascript">

		var wsUri = "ws://"+window.location.host+window.location.pathname;
		var output;
		function init()
		{
			output = document.getElementById("output");
			testWebSocket();
		}
		function testWebSocket()
		{
			websocket = new WebSocket(wsUri);
			websocket.onopen = function(evt) { onOpen(evt) };
			websocket.onclose = function(evt) { onClose(evt) };
			websocket.onmessage = function(evt) { onMessage(evt) };
			websocket.onerror = function(evt) { onError(evt) };
		}
		function onOpen(evt)
		{
			writeToScreen("CONNECTED");
			doSend("WebSocket rocks");
		}
		function onClose(evt)
		{
			writeToScreen("DISCONNECTED");
		}
		function onMessage(evt)
		{
			writeToScreen('<span style="color: blue;">RESPONSE: ' + evt.data+'</span>');
			//websocket.close();
		}
		function onError(evt)
		{
			writeToScreen('<span style="color: red;">ERROR:</span> ' + evt.data);
		}
		function doSend(message)
		{
			writeToScreen("SENT: " + message); 
			websocket.send(message);
		}
		function writeToScreen(message)
		{
			var pre = document.createElement("p");
			pre.style.wordWrap = "break-word";
			pre.innerHTML = message;
			output.appendChild(pre);
		}
		window.addEventListener("load", init, false);
		</script>
	</head>
	<body>
		<h2>WebSocket Test</h2>
		<textarea id="txt"></textarea>
		<button type="button" onclick="doSend(document.getElementById('txt').value);">send</button>
		<div id="output"></div>
	</body>
</html> 
)EOF";

