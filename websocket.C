/*
 * websocket.C
 *
 *  Created on: Jun 1, 2013
 *      Author: xaxaxa
 */
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
#include <cppsp-ng/websocket.H>
#include <cppsp-ng/stringutils.H>
#include <cryptopp/cryptlib.h>
#include <cryptopp/sha.h>
#include <cryptopp/filters.h>
#include <cryptopp/base64.h>
#include <cppsp-ng/cppsp.H>
using namespace CryptoPP;
using namespace CP;

namespace cppsp
{
	using std::length_error;

	static uint64_t htonll(uint64_t value) {
		// The answer is 42
		static const int32_t num = 42;

		// Check the endianness
		if (*reinterpret_cast<const char*>(&num) == num) {
			const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
			const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));

			return (static_cast<uint64_t>(low_part) << 32) | high_part;
		} else {
			return value;
		}
	}


	WebSocketParser::WebSocketParser() {
		bufferSize = 4096;
		buffer = new char[bufferSize];
		reset();
	}
	WebSocketParser::~WebSocketParser() {
		delete[] buffer;
	}
	void WebSocketParser::reset() {
		bufferBegin = bufferEnd = 0;
	}
	// returns old buffer
	char* WebSocketParser::upsize() {
		char* oldBuf = buffer;
		bufferSize *= 2;
		buffer = new char[bufferSize];
		return oldBuf;
	}
	// returns old buffer; always upsizes by at least 2x
	char* WebSocketParser::upsize(int minCapacity) {
		char* oldBuf = buffer;
		bufferSize *= 2;
		while(bufferSize < minCapacity)
			bufferSize *= 2;
		buffer = new char[bufferSize];
		return oldBuf;
	}

	pair<char*,int> WebSocketParser::beginAddData() {
		int curRequestMaxSize = bufferSize - bufferBegin;
		int curRequestSize = bufferEnd - bufferBegin;
		if(curRequestSize <= 0) {
			bufferEnd = bufferBegin = 0;
			return {buffer, bufferSize};
		}
		// there is a partial request in the buffer

		// if the partial frame or complete frame is larger than half the buffer size,
		// upsize the buffer and move the request to the beginning
		if(curRequestSize*2 > bufferSize
				|| (currFrameSizeHint != 0 && currFrameSizeHint*2 > bufferSize)) {
			char* oldBuf = upsize(currFrameSizeHint*2);
			memcpy(buffer, oldBuf + bufferBegin, curRequestSize);
			delete[] oldBuf;
		} else if(bufferBegin > 0) {
			// if the partial request is not at the beginning, move it
			// to the beginning
			memmove(buffer, buffer + bufferBegin, curRequestSize);
		}
		bufferBegin = 0;
		bufferEnd = curRequestSize;

		// the partial request is now at the beginning
		return {buffer + bufferEnd, bufferSize - bufferEnd};
	}

	// place newly read data into view
	void WebSocketParser::endAddData(int len) {
		assert(bufferEnd + len <= bufferSize);
		bufferEnd += len;
	}

	/**
	 read one frame from the buffer; returns whether a frame is
	 read or not.
	 */
	bool WebSocketParser::process(WSFrame& out) {
		char* data = buffer + bufferBegin;
		int len = bufferEnd - bufferBegin;
		int minLen = sizeof(ws_header1);
		if (len < minLen)
			return false;
		ws_header1* h1 = (ws_header1*) data;
		uint8_t pLen1 = h1->payload_len; // & ~(uint8_t) 128;
		//printf("pLen1 = %i\n", pLen1);
		int pLen2 = 0;
		if (pLen1 == 126) pLen2 = 2;
		if (pLen1 == 127) pLen2 = 8;
		minLen += pLen2;
		if (h1->mask) minLen += 4;
		if (len < minLen)
			return false;
		//printf("len = %i\n", len);
		//printf("minLen = %i\n", minLen);
		uint64_t payloadLen;

		switch (pLen1) {
			case 126:
			{
				ws_header_extended16* h2 = (ws_header_extended16*) (h1 + 1);
				payloadLen = ntohs(h2->payload_len);
				break;
			}
			case 127:
			{
				ws_header_extended64* h2 = (ws_header_extended64*) (h1 + 1);
				payloadLen = ntohll(h2->payload_len);
				break;
			}
			default:
				payloadLen = pLen1;
				break;
		}
		currFrameSizeHint = minLen + payloadLen;
		if(currFrameSizeHint > maxFrameSize)
			throw length_error("WebSocketParser: max websocket frame size exceeded");
		//printf("payloadLen = %lli\n", payloadLen);
		if (len < int(minLen + payloadLen))
			return false;
		char* payload = data + minLen;
		out.data = {payload, payloadLen};
		out.fin = h1->fin;
		out.opcode = h1->opcode;
		if (h1->mask) unmask((uint8_t*) payload, payloadLen,
				((ws_footer1*) ((char*) (h1 + 1) + pLen2))->masking_key);
		bufferBegin += currFrameSizeHint;
		currFrameSizeHint = 0;
		return true;
	}
	

	uint8_t* FrameWriter::beginAppend(uint32_t len) {
		int hdrlen = sizeof(WebSocketParser::ws_header1);
		if (len > 125 && len <= 0xFFFF) hdrlen += sizeof(WebSocketParser::ws_header_extended16);
		if (len > 0xFFFF) hdrlen += sizeof(WebSocketParser::ws_header_extended64);
		auto* buf = currBuffer().begin_append(hdrlen + len);
		appendingFrameBegin = currBuffer().length();
		appendingFrameDataLen = len;
		appendingFrameHeaderLen = hdrlen;
		return (uint8_t*) buf + hdrlen;
	}
	void FrameWriter::endAppend(int opcode) {
		typedef WebSocketParser::ws_header1 ws_header1;
		typedef WebSocketParser::ws_header_extended16 ws_header_extended16;
		typedef WebSocketParser::ws_header_extended64 ws_header_extended64;

		char* frame = currBuffer().data() + appendingFrameBegin;
		ws_header1* h1 = ((ws_header1*) frame);
		memset(h1, 0, sizeof(*h1));
		h1->fin = true;
		h1->mask = false;
		h1->opcode = opcode;
		if (appendingFrameDataLen > 125 && appendingFrameDataLen <= 0xFFFF) {
			ws_header_extended16* h2 = (ws_header_extended16*) (h1 + 1);
			h1->payload_len = 126;
			h2->payload_len = htons((uint16_t) appendingFrameDataLen);
		} else if (appendingFrameDataLen > 0xFFFF) {
			ws_header_extended64* h2 = (ws_header_extended64*) (h1 + 1);
			h1->payload_len = 127;
			h2->payload_len = htonll((uint64_t) appendingFrameDataLen);
		} else {
			h1->payload_len = (uint8_t) appendingFrameDataLen;
		}
		uint32_t end = appendingFrameBegin + appendingFrameHeaderLen + appendingFrameDataLen;
		currBuffer().resize(end);
	}
	void FrameWriter::beginFlush() {
		if (writing) {
			writeQueued = true;
			return;
		}
		string_view toWrite = currBuffer();
		if (toWrite.length() <= 0) return;
		writing = true;
		use_buffer2 = !use_buffer2;
		streamWriteAll(toWrite.data(), toWrite.length(), [this](int r) {
			(use_buffer2 ? buffer1 : buffer2).clear();
			writing = false;
			if (r <= 0) {
				closed = true;
				return;
			}
			if (writeQueued) {
				writeQueued = false;
				beginFlush();
			}
		});
	}

	void ws_init(ConnectionHandler& ch, CP::Callback cb) {
		auto& request = ch.request;
		auto& response = ch.response;
		string_view headers_view;
		CP::Callback myCB;

		{
			string_builder headers;
			headers += "HTTP/1.1 101 Switching Protocols\r\n"
						"Connection: Upgrade\r\n"
						"Upgrade: WebSocket\r\n";
			headers += ch.worker->date();

			string s(request.header("sec-websocket-key"));
			s += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

			SHA1 sha1;
			byte tmp[sha1.DigestSize()];
			sha1.CalculateDigest(tmp, (const byte*) s.data(), s.length());

			string encoded;
			StringSource src(tmp, sizeof(tmp), true, new Base64Encoder(new StringSink(encoded), false));
			//printf("Sec-WebSocket-Accept: %s\n",encoded.c_str());

			headers += "Sec-WebSocket-Accept: ";
			headers += encoded;
			headers += "\r\n\r\n";
			headers_view = headers;

			// use the move constructor of string/string_builder to transfer ownership
			// of the headers buffer to the functor, thus preserving the buffer during
			// the write.
			// we enclose the declaration of "headers" in big brackets to make bugs
			// relating to string ownership show up even if the write happened synchronously
			// (which is true most of the time), so that valgrind can catch the error.
			myCB = [h = std::move(headers), cb](int r) {
				cb(r);
			};
		}
		ch.socket.writeAll(headers_view.data(), headers_view.length(), myCB);
	}
	bool ws_iswebsocket(const Request& req) {
		return (ci_compare(req.header("upgrade"), "websocket") == 0);
	}
}

