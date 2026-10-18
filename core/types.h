// Fixed-width integer types for the PC build (MSVC) and the console build
// (xsdk's GCC).
#pragma once

#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
// unsigned long long everywhere, so "%llX" fits it (64-bit Linux's uint64_t
// is unsigned long); MSVC and the console's GCC use this type already.
typedef unsigned long long u64;
typedef int32_t  s32;
typedef int64_t  s64;
