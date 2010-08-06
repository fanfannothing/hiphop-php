/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010 Facebook, Inc. (http://www.facebook.com)          |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/

#ifndef __BASE_H__
#define __BASE_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <vector>
#include <string>
#include <map>
#include <list>
#include <set>
#include <deque>
#include <exception>
#include <ext/hash_map>
#include <ext/hash_set>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/foreach.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/filesystem/operations.hpp>

namespace HPHP {
///////////////////////////////////////////////////////////////////////////////
// system includes

#if defined(__WORDSIZE)
#if __WORDSIZE == 64
#define WORDSIZE_IS_64
#endif
#elif defined(LONG_BIT)
#if LONG_BIT == 64
#define WORDSIZE_IS_64
#endif
#else
#error Cannot determine word size.
#endif

typedef unsigned char uchar;
typedef char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef int32_t int32;
typedef uint32_t uint32;
#ifdef WORDSIZE_IS_64
typedef long long int64;
typedef unsigned long long uint64;
#else
typedef int64_t int64;
typedef uint64_t uint64;
#endif

#ifndef ULLONG_MAX
#define ULLONG_MAX 0xffffffffffffffffULL
#endif

///////////////////////////////////////////////////////////////////////////////
// stl classes

#define hphp_hash_map __gnu_cxx::hash_map
#define hphp_hash_set __gnu_cxx::hash_set
#define hphp_hash     __gnu_cxx::hash

struct ltstr {
  bool operator()(const char *s1, const char *s2) const {
    return strcmp(s1, s2) < 0;
  }
};

struct eqstr {
  bool operator()(const char* s1, const char* s2) const {
    return strcmp(s1, s2) == 0;
  }
};

struct stdltstr {
  bool operator()(const std::string &s1, const std::string &s2) const {
    return strcmp(s1.c_str(), s2.c_str()) < 0;
  }
};

struct stdltistr {
  bool operator()(const std::string &s1, const std::string &s2) const {
    return strcasecmp(s1.c_str(), s2.c_str()) < 0;
  }
};

struct string_hash {
  size_t operator()(const std::string &s) const {
    return __gnu_cxx::__stl_hash_string(s.c_str());
  }
};

template<typename T>
struct pointer_hash {
  size_t operator() (const T *const &p) const {
    return (size_t)p;
  }
};

struct int64_hash {
  size_t operator() (const int64 v) const {
    return (size_t)v;
  }
};

template<typename T>
class hphp_const_char_map :
    public hphp_hash_map<const char *, T, hphp_hash<const char *>, eqstr> {
};

template<typename T>
class hphp_string_map :
    public hphp_hash_map<std::string, T, string_hash> {
};

typedef hphp_hash_set<std::string, string_hash> hphp_string_set;
typedef hphp_hash_set<const char *, hphp_hash<const char *>,
                      eqstr> hphp_const_char_set;

typedef hphp_hash_map<void*, void*, pointer_hash<void> > PointerMap;
typedef hphp_hash_map<void*, int, pointer_hash<void> > PointerCounterMap;
typedef hphp_hash_set<void*, pointer_hash<void> > PointerSet;

typedef std::vector<std::pair<std::string, std::string> > StringPairVec;
typedef std::vector<StringPairVec> StringPairVecVec;

///////////////////////////////////////////////////////////////////////////////
// boost

#ifndef DECLARE_BOOST_TYPES

#define DECLARE_BOOST_TYPES(classname)                                  \
  class classname;                                                      \
  typedef boost::shared_ptr<classname> classname ## Ptr;                \
  typedef boost::weak_ptr<classname> classname ## WeakPtr;              \
  typedef boost::shared_ptr<const classname> classname ## ConstPtr;     \
  typedef std::vector<classname ## Ptr> classname ## PtrVec;            \
  typedef std::set<classname ## Ptr> classname ## PtrSet;               \
  typedef std::list<classname ## Ptr> classname ## PtrList;             \
  typedef std::deque<classname ## Ptr> classname ## PtrQueue;           \
  typedef __gnu_cxx::hash_map<std::string, classname ## Ptr,            \
    string_hash> StringTo ## classname ## PtrMap;                       \
  typedef __gnu_cxx::hash_map<std::string, classname ## PtrVec,         \
    string_hash> StringTo ## classname ## PtrVecMap                     \

#endif

typedef boost::shared_ptr<FILE> FilePtr;

struct null_deleter {
  void operator()(void const *) const {
  }
};

struct file_closer {
  void operator()(FILE *f) const {
    if (f) fclose(f);
  }
};

///////////////////////////////////////////////////////////////////////////////
// debugging

#include <assert.h>

#ifdef RELEASE
#ifndef ALWAYS_ASSERT
#define ASSERT(x)
#else
#define ASSERT(x) assert(x)
#endif
#else
#define ASSERT(x) assert(x)
#endif

///////////////////////////////////////////////////////////////////////////////
}

#endif // __BASE_H__
