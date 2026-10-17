// Small portable SHA-1 (STFS uses SHA-1 for the header hash and hash tables).
#pragma once

#include "types.h"

struct Sha1Context
{
    u32 state[5];
    u64 length;      // total bytes hashed
    u8  buffer[64];
    u32 bufferUsed;
};

void Sha1Init(Sha1Context* ctx);
void Sha1Update(Sha1Context* ctx, const void* data, u32 size);
void Sha1Final(Sha1Context* ctx, u8 digest[20]);

void Sha1(const void* data, u32 size, u8 digest[20]);

void HmacSha1(const u8* key, u32 keySize, const void* data, u32 size, u8 digest[20]);
