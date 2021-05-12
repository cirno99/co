#pragma once

#include "fastring.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace str {

/**
 * split a string 
 *   split("x y z", ' ');    ->  [ "x", "y", "z" ]
 *   split("|x|y|", '|');    ->  [ "", "x", "y" ]
 *   split("xooy", "oo");    ->  [ "x", "y"]
 *   split("xooy", 'o', 1);  ->  [ "x", "oy" ]
 * 
 * @param s  the string, either a null-terminated string or a reference of fastring.
 * @param c  the delimiter, either a single character or a null-terminated string.
 * @param n  max split times, 0 or -1 for unlimited.
 */
std::vector<fastring> split(const char* s, char c, uint32 n=0);
std::vector<fastring> split(const fastring& s, char c, uint32 n=0);
std::vector<fastring> split(const char* s, const char* c, uint32 n=0);

inline std::vector<fastring> split(const fastring& s, const char* c, uint32 n=0) {
    return split(s.c_str(), c, n);
}

/**
 * replace substring in a string with another string 
 *   replace("xooxoox", "oo", "ee");     ->  "xeexeex" 
 *   replace("xooxoox", "oo", "ee", 1);  ->  "xeexoox" 
 * 
 * @param s    the string, either a null-terminated string or a reference of fastring.
 * @param sub  substring to replace.
 * @param to   string replaced to.
 * @param n    max replace times.
 */
fastring replace(const char* s, const char* sub, const char* to, uint32 n=0);
fastring replace(const fastring& s, const char* sub, const char* to, uint32 n=0);

/**
 * strip a string 
 *   strip(" xx\r\n");           ->  "xx"     strip " \t\r\n" by default. 
 *   strip("abxxa", "ab");       ->  "xx"     strip both sides. 
 *   strip("abxxa", "ab", 'l');  ->  "xxa"    strip left only. 
 *   strip("abxxa", "ab", 'r');  ->  "abxx"   strip right only. 
 * 
 * @param s  the string, either a null-terminated string or a reference of fastring.
 * @param c  characters to be stripped, either a single character or a null-terminated string.
 * @param d  direction, 'l' for left, 'r' for right, 'b' for both sides.
 */
fastring strip(const char* s, const char* c=" \t\r\n", char d='b');
fastring strip(const char* s, char c, char d = 'b');
fastring strip(const fastring& s, const char* c=" \t\r\n", char d='b');
fastring strip(const fastring& s, char c, char d='b');
fastring strip(const fastring& s, const fastring& c, char d='b');

/**
 * convert string to built-in types 
 *   - An exception of type const char* will be thrown if any error occured. 
 */
bool to_bool(const char* s);
int32 to_int32(const char* s);
int64 to_int64(const char* s);
uint32 to_uint32(const char* s);
uint64 to_uint64(const char* s);
double to_double(const char* s);

inline bool to_bool(const fastring& s)        { return to_bool(s.c_str()); }
inline bool to_bool(const std::string& s)     { return to_bool(s.c_str()); }
inline int32 to_int32(const fastring& s)      { return to_int32(s.c_str()); }
inline int32 to_int32(const std::string& s)   { return to_int32(s.c_str()); }
inline int64 to_int64(const fastring& s)      { return to_int64(s.c_str()); }
inline int64 to_int64(const std::string& s)   { return to_int64(s.c_str()); }
inline uint32 to_uint32(const fastring& s)    { return to_uint32(s.c_str()); }
inline uint32 to_uint32(const std::string& s) { return to_uint32(s.c_str()); }
inline uint64 to_uint64(const fastring& s)    { return to_uint64(s.c_str()); }
inline uint64 to_uint64(const std::string& s) { return to_uint64(s.c_str()); }
inline double to_double(const fastring& s)    { return to_double(s.c_str()); }
inline double to_double(const std::string& s) { return to_double(s.c_str()); }


/**
 * convert built-in types to string 
 */
template<typename T>
inline fastring from(T t) {
    fastring s(24);
    s << t;
    return s;
}

namespace xx {
inline void dbg(const char* s, fastring& fs) {
    fs << '\"' << s << '\"';
}

inline void dbg(const fastring& s, fastring& fs) {
    fs << '\"' << s << '\"';
}

inline void dbg(const std::string& s, fastring& fs) {
    fs << '\"' << s << '\"';
}

template<typename T>
inline void dbg(const T& t, fastring& fs) {
    fs << t;
}

template<typename K, typename V>
inline void dbg(const std::pair<K, V>& x, fastring& fs) {
    dbg(x.first, fs);
    fs << ':';
    dbg(x.second, fs);
}

template<typename T>
void dbg(const T& beg, const T& end, char c1, char c2, fastring& fs) {
    if (beg == end) {
        fs << c1 << c2;
        return;
    }

    fs << c1;
    for (T it = beg; it != end; ++it) {
        dbg(*it, fs);
        fs << ',';
    }
    fs.back() = c2;
}

template<typename T>
inline fastring dbg(const T& beg, const T& end, char c1, char c2) {
    fastring fs(128);
    dbg(beg, end, c1, c2, fs);
    return fs;
}
} // xx

// convert std::pair to a debug string
template<typename K, typename V>
inline fastring dbg(const std::pair<K, V>& x) {
    fastring fs(64);
    xx::dbg(x, fs);
    return fs;
}

// convert std::vector to a debug string
template<typename T>
inline fastring dbg(const std::vector<T>& v) {
    return xx::dbg(v.begin(), v.end(), '[', ']');
}

// convert std::set to a debug string
template<typename T>
inline fastring dbg(const std::set<T>& v) {
    return xx::dbg(v.begin(), v.end(), '{', '}');
}

// convert std::map to a debug string
template<typename K, typename V>
inline fastring dbg(const std::map<K, V>& v) {
    return xx::dbg(v.begin(), v.end(), '{', '}');
}

// convert std::unordered_set to a debug string
template<typename T>
inline fastring dbg(const std::unordered_set<T>& v) {
    return xx::dbg(v.begin(), v.end(), '{', '}');
}

// convert std::unordered_map to a debug string
template<typename K, typename V>
inline fastring dbg(const std::unordered_map<K, V>& v) {
    return xx::dbg(v.begin(), v.end(), '{', '}');
}

} // namespace str
