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
#include <cpoll-ng/string_builder.H>
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

	using std::string_view;

	// url encode/escaping functions
	int doURLDecode(const char* in, int inLen, char* out);

	int urlDecode(const char* in, int inLen, std::string& out);
	int urlEncode(const char* in, int inLen, std::string& out);
	int htmlEscape(const char* in, int inLen, std::string& out);
	int htmlAttributeEscape(const char* in, int inLen, std::string& out);
	int jsEscape(const char* in_, int inLen, std::string& out);

	int urlDecode(const char* in, int inLen, CP::string_builder& out);
	int urlEncode(const char* in, int inLen, CP::string_builder& out);
	int htmlEscape(const char* in, int inLen, CP::string_builder& out);
	int htmlAttributeEscape(const char* in, int inLen, CP::string_builder& out);
	int jsEscape(const char* in_, int inLen, CP::string_builder& out);

	inline int doURLDecode(const char* in, char* out) { return doURLDecode(in, strlen(in), out); }
	template<class ST> inline int urlDecode(const char* in, ST& out) { return urlDecode(in, strlen(in), out); }
	template<class ST> inline int urlEncode(const char* in, ST& out) { return urlEncode(in, strlen(in), out); }
	template<class ST> inline int htmlEscape(const char* in, ST& out) { return htmlEscape(in, strlen(in), out); }
	template<class ST> inline int htmlAttributeEscape(const char* in, ST& out) { return htmlAttributeEscape(in, strlen(in), out); }
	template<class ST> inline int jsEscape(const char* in, ST& out) { return jsEscape(in, strlen(in), out); }

	inline int doURLDecode(string_view in, char* out) { return doURLDecode(in.data(), in.length(), out); }
	template<class ST> inline int urlDecode(string_view in, ST& out) { return urlDecode(in.data(), in.length(), out); }
	template<class ST> inline int urlEncode(string_view in, ST& out) { return urlEncode(in.data(), in.length(), out); }
	template<class ST> inline int htmlEscape(string_view in, ST& out) { return htmlEscape(in.data(), in.length(), out); }
	template<class ST> inline int htmlAttributeEscape(string_view in, ST& out) { return htmlAttributeEscape(in.data(), in.length(), out); }
	template<class ST> inline int jsEscape(string_view in, ST& out) { return jsEscape(in.data(), in.length(), out); }

	int parseQueryString(const char* in, int inLen, std::string& outBuffer, std::vector<std::tuple<int,int,int,int> >& outIndices);
	int parseQueryString(const char* in, int inLen, CP::string_builder& outBuffer, std::vector<std::tuple<int,int,int,int> >& outIndices);
	int ci_compare(std::string_view s1, std::string_view s2);
	int rfctime2(const tm& time, std::string& out);
	int rfctime(const tm& time, char* out);

	std::string urlDecode(const char* in, int inLen);
	std::string urlEncode(const char* in, int inLen);
	std::string htmlEscape(const char* in, int inLen);
	std::string htmlAttributeEscape(const char* in, int inLen);
	std::string jsEscape(const char* in, int inLen);
}

#endif /* STRINGUTILS_H_ */
