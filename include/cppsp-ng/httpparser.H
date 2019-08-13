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

#ifndef HTTPPARSER_H_
#define HTTPPARSER_H_

#include <string_view>
#include <tuple>
#include <assert.h>

#define CPPSP_MAXHEADERS 128
namespace cppsp
{
	using namespace CP;
	using std::tuple;
	using std::get;

	static constexpr int MAXREQUESTSIZE = 8192;
	struct HTTPParser
	{
		static inline char tolower(char c) {
			if (c <= 'Z' && c >= 'A') c = c - 'A' + 'a';
			return c;
		}
		//s2 must be all lowercase!!!!!
		static bool ci_equals(string_view s1, string_view s2) {
			if (s1.length() != s2.length()) return false;
			if (s1.length() == 0) return true;
			for (int i = 0; i < s1.length(); i++) {
				if (tolower(s1.data()[i]) != s2.data()[i]) return false;
			}
			return true;
		}

		int requestLineStart, requestLineEnd;
		int verbStart, verbEnd;
		int hostStart, hostEnd;
		int pathStart, pathEnd;
		vector<tuple<int,int,int,int> > headers;

		// state variables
		char* buffer;
		int bufferSize;
		int bufferBegin;
		int bufferEnd;
		int bufferProcessed;
		int currContentLength;
		bool malformed;
		enum {
			READHEADERS,
			READCONTENT
		} state;


		string_view requestLine() {
			return slice(bufferBegin + requestLineStart, bufferBegin + requestLineEnd);
		}
		string_view verb() {
			return slice(bufferBegin + verbStart, bufferBegin + verbEnd);
		}
		string_view host() {
			return slice(bufferBegin + hostStart, bufferBegin + hostEnd);
		}
		string_view path() {
			return slice(bufferBegin + pathStart, bufferBegin + pathEnd);
		}
		inline pair<string_view,string_view> header(const tuple<int,int,int,int>& item) {
			int nS = get<0>(item), nE = get<1>(item);
			int vS = get<2>(item), vE = get<3>(item);
			return {slice(bufferBegin + nS, bufferBegin + nE),
					slice(bufferBegin + vS, bufferBegin + vE)};
		}

		HTTPParser() {
			bufferSize = 4096;
			buffer = new char[bufferSize];
		}
		~HTTPParser() {
			delete[] buffer;
		}
		void reset() {
			bufferBegin = bufferEnd = bufferProcessed = 0;
			clearRequest();
		}
		// returns old buffer
		char* upsize() {
			char* oldBuf = buffer;
			bufferSize *= 2;
			buffer = new char[bufferSize];
			return oldBuf;
		}

		void clearRequest() {
			currContentLength = 0;
			requestLineStart = requestLineEnd = -1;
			verbStart = verbEnd = -1;
			hostStart = hostEnd = -1;
			pathStart = pathEnd = -1;
			bufferBegin = bufferProcessed;
			headers.clear();
			malformed = false;
		}

		pair<char*,int> beginAddData() {
			int curRequestMaxSize = bufferSize - bufferBegin;
			int curRequestSize = bufferEnd - bufferBegin;
			if(curRequestSize <= 0) {
				bufferEnd = bufferBegin = bufferProcessed = 0;
				return {buffer, bufferSize};
			}
			// there is a partial request in the buffer

			// if the partial request is larger than half the buffer size,
			// upsize the buffer and move the request to the beginning
			if(curRequestSize*2 > bufferSize) {
				char* oldBuf = upsize();
				memcpy(buffer, oldBuf + bufferBegin, curRequestSize);
				delete[] oldBuf;
			} else if(bufferBegin > 0) {
				// if the partial request is not at the beginning, move it
				// to the beginning
				memmove(buffer, buffer + bufferBegin, curRequestSize);
			}
			bufferProcessed -= bufferBegin;
			bufferBegin = 0;
			bufferEnd = curRequestSize;

			// the partial request is now at the beginning
			return {buffer + bufferEnd, bufferSize - bufferEnd};
		}
		// place newly read data into view
		void endAddData(int len) {
			assert(bufferEnd + len <= bufferSize);
			bufferEnd += len;
		}
		// look for a usable http request in the buffer
		bool readRequest() {
			int curRequestSize = bufferEnd - bufferBegin;
			if(curRequestSize >= MAXREQUESTSIZE) {
				malformed = true;
				return true;
			}
			if(state == READHEADERS) {
				while(bufferProcessed < bufferEnd) {
					// continue searching for crlf from where we left off last time
					int index = findCRLF(bufferProcessed, bufferEnd);
					if(index < 0) return false;
					// found a crlf; process the header line

					// reached double crlf
					if(bufferProcessed == index) {
						bufferProcessed = index + 2;

						if(requestLineStart == -1)
							malformed = true;

						if(ci_equals(verb(), "get") || currContentLength == 0) {
							return true;
						}
						state = READCONTENT;
						goto readContent;
					}
					addHeader(bufferProcessed, index);

					// skip to after the crlf
					bufferProcessed = index + 2;
				}
			} else { // READCONTENT
			readContent:
				int headerSize = bufferProcessed - bufferBegin;
				int contentsRead = bufferEnd - bufferProcessed;

				if(headerSize + currContentLength > MAXREQUESTSIZE)
					throw runtime_error("HTTPParser: max request size exceeded");
				return (contentsRead >= currContentLength);
			}
			return false;
		}
		int findCRLF(int begin, int end) {
			void* res = memmem(&buffer[begin], end-begin, "\r\n", 2);
			if(res == nullptr) return -1;
			return ((char*)res) - &buffer[0];
		}
		int findChar(int begin, int end, char ch) {
			if(end < begin) return -1;
			void* res = memchr(&buffer[begin], ch, end-begin);
			if(res == nullptr) return -1;
			return ((char*)res) - &buffer[0];
		}
		inline string_view slice(int begin, int end) {
			if(end <= begin) return string_view();
			return string_view(&buffer[0] + begin, end - begin);
		}
		inline string_view trim(string_view s) {
			const char* first = s.data();
			const char* last = first + s.length() - 1;
			while(first <= last && isspace(*first))
				first++;
			while(first <= last && isspace(*last))
				last--;
			return string_view(first, last-first+1);
		}
		inline void trim(int& start, int& end) {
			const char* first = buffer + start;
			const char* last = buffer + end - 1;
			while(first <= last && isspace(*first))
				first++;
			while(first <= last && isspace(*last))
				last--;
			start = first - buffer;
			end = last - buffer + 1;
		}
		void addHeader(int begin, int end) {
			if(requestLineStart == -1) {
				requestLineStart = begin - bufferBegin;
				requestLineEnd = end - bufferBegin;
				int index = findChar(begin, end, ' ');
				if(index < 0) {
					malformed = true;
					return;
				}
				verbStart = begin - bufferBegin;
				verbEnd = index - bufferBegin;

				int index2 = findChar(index + 1, end, ' ');
				if(index2 < 0) {
					malformed = true;
					return;
				}
				pathStart = index + 1 - bufferBegin;
				pathEnd = index2 - bufferBegin;
			} else {
				int index = findChar(begin, end, ':');
				if(index < 0) {
					malformed = true;
					return;
				}
				int nS = begin, nE = index, vS = index+1, vE = end;
				trim(nS, nE);
				trim(vS, vE);
				string_view name = slice(nS, nE);
				if(ci_equals(name, "content-length")) {
					buffer[vE] = 0;
					currContentLength = atoi(&buffer[vS]);
				}
				if(ci_equals(name, "host")) {
					hostStart = vS;
					hostEnd = vE;
				}
				headers.push_back({nS - bufferBegin, nE - bufferBegin,
									vS - bufferBegin, vE - bufferBegin});
			}
		}
		
	};
}

#endif /* HTTPPARSER_H_ */
