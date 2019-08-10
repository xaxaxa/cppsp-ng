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
/*
 * stringutils.C
 *
 *  Created on: Apr 9, 2013
 *      Author: xaxaxa
 */
#include <cpoll-ng/cpoll.H>
#include <cppsp-ng/stringutils.H>
#include <cppsp-ng/split.H>
#include <stdarg.h>
#include <assert.h>
#include <math.h>

using namespace CP;
namespace cppsp
{
	inline char hexCharToInt(char ch) {
		if (ch <= '9') return ch - '0';
		else if (ch <= 'Z') return ch - 'A' + 10;
		else return ch - 'a' + 10;
	}
	inline char intToHexChar(char i) {
		if (i < 10) return i + '0';
		else return i - 10 + 'A';
	}
	static inline void _urldecode_memcpy(void* dst, const void* src, int len) {
		char* d = (char*) dst;
		char* s = (char*) src;
		for (int i = 0; i < len; i++)
			d[i] = (s[i] == '+') ? ' ' : s[i];
	}
	int doURLDecode(const char* in, int inLen, char* out) {
		//XXX: dangerous (potentially exploitable) codepath; please audit
		char* c = out; //points to next byte to be written
		const char* end = in + inLen; //end of input array
		const char* ptr = in; //current read position
		while (ptr < end) {
			const char* next = (const char*) memchr(ptr, '%', end - ptr);
			if (next == NULL) {
				_urldecode_memcpy(c, ptr, end - ptr);
				c += (end - ptr);
				break;
			}
			_urldecode_memcpy(c, ptr, next - ptr); //write out everything between the read position and the '%'
			c += (next - ptr);
			if (next + 2 >= end) { //there isn't 2 bytes after the '%'
				_urldecode_memcpy(c, next, end - next);
				c += (end - next);
				break;
			}
			*c = hexCharToInt(next[1]) << 4 | hexCharToInt(next[2]);
			c++;
			ptr = next + 3;
		}
		return int(c - out);
	}
	int urlDecode(const char* in, int inLen, string& out) {
		int oldLen = out.length();
		out.resize(oldLen + inLen);
		char* ch = out.data() + oldLen;
		int len = doURLDecode(in, inLen, ch);
		out.resize(oldLen + len);
		return len;
	}
	int urlEncode(const char* in, int inLen, string& out) {
		int last_i = 0;
		const char* c = in;
		char ch[3];
		ch[0] = '%';
		int asdf = 0;
		for (int i = 0; i < inLen; i++) {
			if ((48 <= c[i] && c[i] <= 57) || //0-9
					(65 <= c[i] && c[i] <= 90) || //abc...xyz
					(97 <= c[i] && c[i] <= 122) || //ABC...XYZ
					(c[i] == '~' || c[i] == '!' || c[i] == '*' || c[i] == '(' || c[i] == ')'
							|| c[i] == '\'')) continue;
			if (i > last_i) out.append(in + last_i, i - last_i);
			last_i = i + 1;
			ch[1] = intToHexChar(c[i] >> 4);
			ch[2] = intToHexChar(c[i] & (char) 0xF);
			out.append(ch, 3);
			asdf += 2;
		}
		if (inLen > last_i) out.append(in + last_i, inLen - last_i);
		return inLen + asdf;
	}
	std::string urlDecode(const char* in, int inLen) {
		string ret;
		urlDecode(in, inLen, ret);
		return ret;
	}
	std::string urlEncode(const char* in, int inLen) {
		string ret;
		urlEncode(in, inLen, ret);
		return ret;
	}
	std::string htmlEscape(const char* in, int inLen) {
		string ret;
		htmlEscape(in, inLen, ret);
		return ret;
	}
	std::string htmlAttributeEscape(const char* in, int inLen) {
		string ret;
		htmlAttributeEscape(in, inLen, ret);
		return ret;
	}
	int parseQueryString(const char* in, int inLen, string& outBuffer, vector<tuple<int,int,int,int> >& outIndices) {
		//XXX: dangerous (potentially exploitable) codepath; please audit
		split spl(in, inLen, '&');
		int ret = 0;

		// split input string by &
		while (spl.read()) {
			const char* start = spl.value.data();
			int len = spl.value.length();
			if(len == 0) continue;
			const char* end = start + len;
			const char* equ = (const char*) memchr(start, '=', len);

			// "=" not found; assume empty value
			if (equ == NULL) {
				int nS = outBuffer.length();
				urlDecode(start, len, outBuffer);
				int nE = outBuffer.length();
				outIndices.push_back({nS, nE, -1, -1});
			} else {
				int nS = outBuffer.length();
				urlDecode(start, equ - start, outBuffer);
				int nE = outBuffer.length();
				int vS = nE;
				urlDecode(equ + 1, end - equ - 1, outBuffer);
				int vE = outBuffer.length();
				outIndices.push_back({nS, nE, vS, vE});
			}
			ret++;
		}
		return ret;
	}
	int htmlEscape(const char* in, int inLen, string& out) {
		//XXX: dangerous (potentially exploitable) codepath; please audit
		int sz = 0;
		for (int i = 0; i < inLen; i++) {
			switch (in[i]) {
				case '&':
					sz += 5;
					break;
				case '<':
					sz += 4;
					break;
				case '>':
					sz += 4;
					break;
				default:
					sz++;
					break;
			}
		}

		int oldLen = out.length();
		out.resize(oldLen + sz);
		char* data = out.data() + oldLen;
		char* c = data;
		for (int i = 0; i < inLen; i++) {
			switch (in[i]) {
				case '&':
					c[0] = '&';
					c[1] = 'a';
					c[2] = 'm';
					c[3] = 'p';
					c[4] = ';';
					c += 5;
					break;
				case '<':
					c[0] = '&';
					c[1] = 'l';
					c[2] = 't';
					c[3] = ';';
					c += 4;
					break;
				case '>':
					c[0] = '&';
					c[1] = 'g';
					c[2] = 't';
					c[3] = ';';
					c += 4;
					break;
				default:
					*(c++) = in[i];
			}
		}
		return sz;
	}
	int htmlAttributeEscape(const char* in, int inLen, string& out) {
		//XXX: dangerous (potentially exploitable) codepath; please audit
		int last_i = 0;
		int oldLen = out.length();
		const char* tmp;
		for (int i = 0; i < inLen; i++) {
			switch (in[i]) {
				case '&':
					tmp = "&amp;";
					break;
				case '<':
					tmp = "&lt;";
					break;
				case '>':
					tmp = "&gt;";
					break;
				case '"':
					tmp = "&quot;";
					break;
				case '\'':
					tmp = "&apos;";
					break;
				default:
					continue;
			}
			if (i > last_i) out.append(in + last_i, i - last_i);
			last_i = i + 1;
			out.append(tmp);
		}
		if (inLen > last_i) out.append(in + last_i, inLen - last_i);
		return out.length() - oldLen;
	}

	int jsEscape(const char* in_, int inLen, string& out) {
		//XXX: dangerous (potentially exploitable) codepath; please audit
		uint8_t* in = (uint8_t*) in_;
		int sz = 0;
		for (int i = 0; i < inLen; i++) {
			if (isalnum(in[i])) sz++;
			else sz += 6;
		}

		int oldLen = out.length();
		out.resize(oldLen + sz);
		char* data = out.data() + oldLen;
		char* c = data;
		for (int i = 0; i < inLen; i++) {
			uint8_t ch = (uint8_t) in[i];
			if (isalnum(in[i])) *(c++) = in[i];
			else {
				c[0] = '\\';
				c[1] = 'u';
				c[2] = '0';
				c[3] = '0';
				c[4] = intToHexChar(in[i] >> 4);
				c[5] = intToHexChar(in[i] & 0xF);
				c += 6;
			}
		}
		return sz;
	}
	std::string jsEscape(const char* in, int inLen) {
		string ret;
		jsEscape(in, inLen, ret);
		return ret;
	}

	int ci_compare(string_view s1, string_view s2) {
		if (s1.length() > s2.length()) return 1;
		if (s1.length() < s2.length()) return -1;
		if (s1.length() == 0) return 0;
		char a, b;
		for (int i = 0; i < s1.length(); i++) {
			a = tolower(s1.data()[i]);
			b = tolower(s2.data()[i]);
			if (a < b) return -1;
			if (a > b) return 1;
		}
		return 0;
	}

	//inline-able memcpy() for copying SHORT STRINGS ONLY
	static inline void memcpy2(void* dst, const void* src, int len) {
		for (int i = 0; i < len; i++)
			((char*) dst)[i] = ((const char*) src)[i];
	}

	static inline int itoa1(int i, char* b) {
		static char const digit[] = "0123456789";
		char* p = b;
		//negative detection is not needed for this specific use-case
		//(writing the content-length header)
		/*if (i < 0) {
		 *p++ = '-';
		 i = -i;
		 }*/
		p += (i == 0 ? 0 : int(log10f(i))) + 1;
		*p = '\0';
		int l = p - b;
		do { //Move back, inserting digits as u go
			*--p = digit[i % 10];
			i = i / 10;
		} while (i);
		return l;
	}
	//pads beginning with 0s
	//i: input number
	//d: # of digits
	static inline int itoa2(int i, int d, char* b) {
		static char const digit[] = "0123456789";
		for (int x = d - 1; x >= 0; x--) {
			b[x] = digit[i % 10];
			i /= 10;
		}
		return d;
	}

	//#define LITTLE_ENDIAN
	constexpr uint32_t operator "" _s4 (const char *str, size_t len) {
		assert(len == 4);
		#ifdef LITTLE_ENDIAN
			return (str[3] << 24)
				| (uint32_t(str[2]) << 16)
				| (uint32_t(str[1]) << 8)
				| uint32_t(str[0]);
		#else
			return (str[0] << 24)
				| (uint32_t(str[1]) << 16)
				| (uint32_t(str[2]) << 8)
				| uint32_t(str[3]);
		#endif
	}
	constexpr uint32_t EN(uint32_t val) {
		#ifdef LITTLE_ENDIAN
			return (val << 24)
					| ((val & 0x0000ff00) << 8)
					| ((val & 0x00ff0000) >> 8)
					| (val >> 24);
		#else
			return val;
		#endif
	}
	static inline uint32_t CTW(char ch, int pos) {
		#ifdef LITTLE_ENDIAN
			return uint32_t(ch) << ((3-pos)*8);
		#else
			return uint32_t(ch) << (pos*8);
		#endif
	}
	static inline uint32_t SHL(uint32_t s, int chars) {
		#ifdef LITTLE_ENDIAN
			return s >> (chars*8);
		#else
			return s << (chars*8);
		#endif
	}

	// 4-digit itoa, zero filling
	static inline uint32_t itoa4(int x) {
		int a = (x/1000) % 10;
		int b = (x/100) % 10;
		int c = (x/10) % 10;
		int d = x % 10;

		uint32_t ret = 0;
		#ifdef LITTLE_ENDIAN
			ret |= (d + '0') << 24;
			ret |= (c + '0') << 16;
			ret |= (b + '0') << 8;
			ret |= (a + '0') << 0;
		#else
			ret |= (a + '0') << 24;
			ret |= (b + '0') << 16;
			ret |= (c + '0') << 8;
			ret |= (d + '0') << 0;
		#endif
		return ret;
	}
	int rfctime2(const tm& time, string& out) {
		static const uint32_t dayOfWeek[] = { "Sun,"_s4, "Mon,"_s4, "Tue,"_s4, "Wed,"_s4, "Thu,"_s4, "Fri,"_s4, "Sat,"_s4};
		static const uint32_t ddigits[] = {
			" 00 "_s4, " 01 "_s4, " 02 "_s4, " 03 "_s4, " 04 "_s4, " 05 "_s4,
			" 06 "_s4, " 07 "_s4, " 08 "_s4, " 09 "_s4, " 10 "_s4, " 11 "_s4,
			" 12 "_s4, " 13 "_s4, " 14 "_s4, " 15 "_s4, " 16 "_s4, " 17 "_s4,
			" 18 "_s4, " 19 "_s4, " 20 "_s4, " 21 "_s4, " 22 "_s4, " 23 "_s4,
			" 24 "_s4, " 25 "_s4, " 26 "_s4, " 27 "_s4, " 28 "_s4, " 29 "_s4,
			" 30 "_s4, " 31 "_s4, " 32 "_s4, " 33 "_s4, " 34 "_s4, " 35 "_s4,
			" 36 "_s4, " 37 "_s4, " 38 "_s4, " 39 "_s4, " 40 "_s4, " 41 "_s4,
			" 42 "_s4, " 43 "_s4, " 44 "_s4, " 45 "_s4, " 46 "_s4, " 47 "_s4,
			" 48 "_s4, " 49 "_s4, " 50 "_s4, " 51 "_s4, " 52 "_s4, " 53 "_s4,
			" 54 "_s4, " 55 "_s4, " 56 "_s4, " 57 "_s4, " 58 "_s4, " 59 "_s4
		};
		static const uint32_t months[] = {
			"Jan "_s4, "Feb "_s4, "Mar "_s4, "Apr "_s4, "May "_s4, "Jun "_s4,
			"Jul "_s4, "Aug "_s4, "Sep "_s4, "Oct "_s4, "Nov "_s4, "Dec "_s4
		};

		//####%%%%####%%%%####%%%%####%%%%
		//WWW, DD MMM YYYY HH:mm:ss GMT\0
		uint32_t buf[8];
		buf[0] = dayOfWeek[time.tm_wday];
		buf[1] = ddigits[time.tm_mday];
		buf[2] = months[time.tm_mon];
		buf[3] = itoa4(time.tm_year + 1900);
		buf[4] = (ddigits[time.tm_hour] & EN(0xffffff00)) | EN(':');
		buf[5] = (SHL(ddigits[time.tm_min], 1) & EN(0xffff0000))
				| EN(uint32_t(':') << 8)
				| CTW((time.tm_sec/10) + '0', 0);
		buf[6] = CTW((time.tm_sec % 10) + '0', 3)
				| ("  GM"_s4 % EN(0x00ffffff));
		buf[7] = "T\0\0\0"_s4;
		out.append((char*)buf, 29);
		return 29;
	}
	int rfctime(const tm& time, char* c) {
		static const char* days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
		static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
				"Oct", "Nov", "Dec" };
		char* s = c;
		//AAA, AA AAA ???? AA:AA:AA GMT\0
		const char* day = days[time.tm_wday];
		//copy 4 bytes (includes extra null byte)
		*(int*) c = (*(int*) day) | int(',') << 24;
		c += 4;
		*(c++) = ' ';
		c += itoa1(time.tm_mday, c);
		*(c++) = ' ';
		const char* month = months[time.tm_mon];
		*(c++) = *(month++);
		*(c++) = *(month++);
		*(c++) = *(month++);
		*(c++) = ' ';
		c += itoa1(time.tm_year + 1900, c);
		*(c++) = ' ';
		c += itoa2(time.tm_hour, 2, c);
		*(c++) = ':';
		c += itoa2(time.tm_min, 2, c);
		*(c++) = ':';
		c += itoa2(time.tm_sec, 2, c);
		*(c++) = ' ';
		*(c++) = 'G';
		*(c++) = 'M';
		*(c++) = 'T';
		*(c++) = '\0';
		return int(c - s) - 1;
	}
}

