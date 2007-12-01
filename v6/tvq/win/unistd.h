/* unistd.h - this file is for MSVC only, it contains only what tvq needs! */

#if defined(_WIN32) && !defined(WIN32)
#define WIN32 1
#endif

#ifdef _MSC_VER
typedef unsigned __int64 uint64_t;
typedef          __int64 int64_t;
typedef unsigned __int32 uint32_t;
typedef          __int32 int32_t;
typedef unsigned __int16 uint16_t;
typedef          __int16 int16_t;
/*typedef unsigned __int8  uint8_t;*/
typedef          __int8  int8_t;
#define strcasecmp _stricmp
#endif
