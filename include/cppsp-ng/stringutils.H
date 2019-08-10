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
 * stringutils.H
 *
 *  Created on: Apr 9, 2013
 *      Author: xaxaxa
 */

#ifndef STRINGUTILS_H_
#define STRINGUTILS_H_
#include <cpoll-ng/cpoll.H>
#include <string>
namespace cppsp
{
#ifndef __CPPSP_TOLOWER
#define __CPPSP_TOLOWER
	static inline char tolower(char c) {
		if (c <= 'Z' && c >= 'A') c = c - 'A' + 'a';
		return c;
	}
#endif

	// url encode/escaping functions
	int doURLDecode(const char* in, int inLen, char* out);
	int urlDecode(const char* in, int inLen, string& out);
	int urlEncode(const char* in, int inLen, string& out);
	int htmlEscape(const char* in, int inLen, string& out);
	int htmlAttributeEscape(const char* in, int inLen, string& out);
	int jsEscape(const char* in_, int inLen, string& out);

	inline int doURLDecode(const char* in, char* out) { return doURLDecode(in, strlen(in), out); }
	inline int urlDecode(const char* in, string& out) { return urlDecode(in, strlen(in), out); }
	inline int urlEncode(const char* in, string& out) { return urlEncode(in, strlen(in), out); }
	inline int htmlEscape(const char* in, string& out) { return htmlEscape(in, strlen(in), out); }
	inline int htmlAttributeEscape(const char* in, string& out) { return htmlAttributeEscape(in, strlen(in), out); }
	inline int jsEscape(const char* in, string& out) { return jsEscape(in, strlen(in), out); }

	inline int doURLDecode(string_view in, char* out) { return doURLDecode(in.data(), in.length(), out); }
	inline int urlDecode(string_view in, string& out) { return urlDecode(in.data(), in.length(), out); }
	inline int urlEncode(string_view in, string& out) { return urlEncode(in.data(), in.length(), out); }
	inline int htmlEscape(string_view in, string& out) { return htmlEscape(in.data(), in.length(), out); }
	inline int htmlAttributeEscape(string_view in, string& out) { return htmlAttributeEscape(in.data(), in.length(), out); }
	inline int jsEscape(string_view in, string& out) { return jsEscape(in.data(), in.length(), out); }

	int parseQueryString(const char* in, int inLen, string& outBuffer, vector<tuple<int,int,int,int> >& outIndices);
	int ci_compare(string_view s1, string_view s2);
	int rfctime2(const tm& time, string& out);
	int rfctime(const tm& time, char* out);

	std::string urlDecode(const char* in, int inLen);
	std::string urlEncode(const char* in, int inLen);
	std::string htmlEscape(const char* in, int inLen);
	std::string htmlAttributeEscape(const char* in, int inLen);
	std::string jsEscape(const char* in, int inLen);
}

#endif /* STRINGUTILS_H_ */
