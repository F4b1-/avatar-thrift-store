#include "sha1.h"
#include "bytes.h"

#include <string.h>

static u32 Rol(u32 v, int n) { return (v << n) | (v >> (32 - n)); }

static void Sha1Block(u32 state[5], const u8* block)
{
    u32 w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ReadBE32(block + i * 4);
    for (int i = 16; i < 80; i++)
        w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    u32 a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; i++)
    {
        u32 f, k;
        if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }

        u32 t = Rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = Rol(b, 30); b = a; a = t;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void Sha1Init(Sha1Context* ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->length = 0;
    ctx->bufferUsed = 0;
}

void Sha1Update(Sha1Context* ctx, const void* data, u32 size)
{
    const u8* p = (const u8*)data;
    ctx->length += size;

    while (size > 0)
    {
        u32 take = 64 - ctx->bufferUsed;
        if (take > size)
            take = size;
        memcpy(ctx->buffer + ctx->bufferUsed, p, take);
        ctx->bufferUsed += take;
        p += take;
        size -= take;

        if (ctx->bufferUsed == 64)
        {
            Sha1Block(ctx->state, ctx->buffer);
            ctx->bufferUsed = 0;
        }
    }
}

void Sha1Final(Sha1Context* ctx, u8 digest[20])
{
    u64 bitLength = ctx->length * 8;

    u8 pad = 0x80;
    Sha1Update(ctx, &pad, 1);
    u8 zero = 0;
    while (ctx->bufferUsed != 56)
        Sha1Update(ctx, &zero, 1);

    u8 lengthBytes[8];
    WriteBE64(lengthBytes, bitLength);
    Sha1Update(ctx, lengthBytes, 8);

    for (int i = 0; i < 5; i++)
        WriteBE32(digest + i * 4, ctx->state[i]);
}

void Sha1(const void* data, u32 size, u8 digest[20])
{
    Sha1Context ctx;
    Sha1Init(&ctx);
    Sha1Update(&ctx, data, size);
    Sha1Final(&ctx, digest);
}

void HmacSha1(const u8* key, u32 keySize, const void* data, u32 size, u8 digest[20])
{
    u8 block[64];
    memset(block, 0, sizeof(block));
    if (keySize > 64)
        Sha1(key, keySize, block);
    else
        memcpy(block, key, keySize);

    u8 pad[64];
    for (int i = 0; i < 64; i++)
        pad[i] = block[i] ^ 0x36;
    u8 inner[20];
    Sha1Context ctx;
    Sha1Init(&ctx);
    Sha1Update(&ctx, pad, 64);
    Sha1Update(&ctx, data, size);
    Sha1Final(&ctx, inner);

    for (int i = 0; i < 64; i++)
        pad[i] = block[i] ^ 0x5C;
    Sha1Init(&ctx);
    Sha1Update(&ctx, pad, 64);
    Sha1Update(&ctx, inner, 20);
    Sha1Final(&ctx, digest);
}
