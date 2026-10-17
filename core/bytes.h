// Endian-explicit reads/writes. Everything is done byte by byte, so the same
// code is correct on little-endian x86 (PC) and big-endian PowerPC (Xbox 360).
#pragma once

#include "types.h"

inline u16 ReadBE16(const u8* p) { return (u16)((p[0] << 8) | p[1]); }
inline u32 ReadBE24(const u8* p) { return ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2]; }
inline u32 ReadBE32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
inline u64 ReadBE64(const u8* p) { return ((u64)ReadBE32(p) << 32) | ReadBE32(p + 4); }

inline u16 ReadLE16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 ReadLE24(const u8* p) { return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16); }

inline void WriteBE32(u8* p, u32 v)
{
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

inline void WriteBE64(u8* p, u64 v)
{
    WriteBE32(p, (u32)(v >> 32));
    WriteBE32(p + 4, (u32)v);
}
