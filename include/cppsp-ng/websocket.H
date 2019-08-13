/*
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * */

#include <cpoll-ng/cpoll.H>
#include <cpoll-ng/string_builder.H>
#include <assert.h>

using namespace CP;
namespace cppsp
{
	using std::pair;
	using std::function;
	using std::string_view;
	/**
	  Usage:
	  	WebSocketParser maintains an internal, dynamically sized buffer.
	  	To begin reading websocket data from an arbitrary stream, call
	  	beginPutData() to reserve space in the buffer, put your raw
	  	received data in the buffer, and call endPutData() with the
	  	number of bytes actually put. Then repeatedly call process()
	  	until it returns false. Each time process() returns true, it
	  	has parsed one websocket frame and put it in "out".
	  
	  Example:
	  	WebSocketParser parser;
	  
	  	while(true) {
	  		// allocate space in the buffer
	  		String buf = parser.beginPutData(4096);
	  		// read data into buffer
	  		int bytesRead = (read some data into buf and return bytes read);
	  		// inform parser of the new data
	  		parser.endPutData(bytesRead);
	  
	  		// run the actual parser
	  		WSFrame frame;
	  		while(parser.process(&frame)) {
	  			// do stuff with frame
	  		}
	  	}
	 */
	struct WebSocketParser
	{
		struct ws_header1
		{
			//char flags:8;
			unsigned int opcode :4;
			bool rsv1 :1;
			bool rsv2 :1;
			bool rsv3 :1;
			bool fin :1;

			unsigned int payload_len :7;
			bool mask :1;
		}__attribute__((packed));
		struct ws_footer1
		{
			uint32_t masking_key;
		}__attribute__((packed));

		struct ws_header_extended16
		{
			uint16_t payload_len;
		}__attribute__((packed));

		struct ws_header_extended64
		{
			uint64_t payload_len;
		}__attribute__((packed));

		struct WSFrame
		{
			string_view data;
			char opcode;
			bool fin;
		};

		char* buffer;
		// bufferBegin is the start of the current frame
		int bufferBegin, bufferEnd, bufferSize;
		int currFrameSizeHint = 0;
		int maxFrameSize = 1024*16;

		WebSocketParser();
		~WebSocketParser();

		void reset();

		// returns old buffer
		char* upsize();

		// returns old buffer; always upsizes by at least 2x
		char* upsize(int minCapacity);

		// returns a buffer for reading
		pair<char*,int> beginAddData();

		// place newly read data into view
		void endAddData(int len);

		inline void unmask(uint8_t* data, int len, uint32_t key) {
			/*uint32_t* d = (uint32_t*) data.data();
			 int len = data.length() / sizeof(*d);
			 for (int i = 0; i < len; i++) {
			 d[i] ^= key;
			 }
			 uint8_t* tmp = (uint8_t*) (d + len);
			 uint8_t* tmp1 = (uint8_t*) &key;
			 int leftover = data.length() % sizeof(*d);
			 if (leftover > 0) tmp[0] ^= tmp1[0];
			 if (leftover > 1) tmp[1] ^= tmp1[1];
			 if (leftover > 2) tmp[2] ^= tmp1[2];
			 if (leftover > 3) tmp[3] ^= tmp1[3];*/
			uint8_t* k = (uint8_t*) &key;
			for (int i = 0; i < len; i++) {
				data[i] = data[i] ^ k[i % sizeof(key)];
			}
		}
		/**
		 read one frame from the buffer; returns whether a frame is
		 read or not.
		 */
		bool process(WSFrame& out);
	};

	class FrameWriter
	{
	public:
		typedef string_builder str;
		static const uint32_t npos = (uint32_t)-1;

		str buffer1, buffer2;
		function<void(const void* buf, int len, const CP::Callback& cb)> streamWriteAll;

		uint32_t appendingFrameBegin = 0;
		uint32_t appendingFrameDataLen = 0;
		uint32_t appendingFrameHeaderLen = 0;
		bool use_buffer2 = false;
		bool closed = false;
		bool writing = false;
		bool writeQueued = false;
		inline str& currBuffer() {
			return use_buffer2 ? buffer2 : buffer1;
		}
		/**
		 Prepare for the insertion of a chunk into the queue;
		 @return the allocated buffer space; may be larger than the requested length
		 Calling beginInsert again before calling endInsert will replace the current
		 request with the new one.
		 */
		uint8_t* beginAppend(uint32_t len);
		/**
		 Complete the insertion of a chunk.
		 */
		void endAppend(int opcode);
		uint32_t bytesPending() {
			return (uint32_t) currBuffer().length();
		}
		void flush() {
			beginFlush();
		}
		void beginFlush();
	};
	class Request;
	class Response;
	class ConnectionHandler;

	/**
	 perform the websocket handshake, and call cb when done
	 */
	void ws_init(ConnectionHandler& ch, CP::Callback cb);
	/**
	 check http headers to see if the client is attempting to start
	 a websocket connection
	 */
	bool ws_iswebsocket(const Request& req);
}
