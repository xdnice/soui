#ifndef _LIB
#ifdef UTILITIES_EXPORTS
#define UTILITIES_API __declspec(dllexport)
#else
#define UTILITIES_API __declspec(dllimport)
#endif
#else
#define UTILITIES_API
#endif

#ifndef ASSERT
#include <assert.h>
#define ASSERT(x) assert(x)
#define ASSERT_NE(a,b) ASSERT(a!=b)
#endif