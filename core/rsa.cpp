#include "rsa.h"

#include <string.h>
#include <vector>

// Numbers are little-endian arrays of 32-bit limbs, all as long as the modulus
// plus one limb, so a shifted value never overflows during reduction.
typedef std::vector<u32> Num;

static Num FromBigEndian(const u8* p, u32 bytes, u32 limbs)
{
    Num n(limbs, 0);
    for (u32 i = 0; i < bytes; i++)
    {
        u32 bit = (bytes - 1 - i) * 8;
        n[bit / 32] |= (u32)p[i] << (bit % 32);
    }
    return n;
}

static void ToBigEndian(const Num& n, u8* out, u32 bytes)
{
    for (u32 i = 0; i < bytes; i++)
    {
        u32 bit = (bytes - 1 - i) * 8;
        out[i] = (u8)(n[bit / 32] >> (bit % 32));
    }
}

static int Compare(const Num& a, const Num& b)
{
    for (size_t i = a.size(); i-- > 0;)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

static void SubtractInPlace(Num& a, const Num& b)
{
    u64 borrow = 0;
    for (size_t i = 0; i < a.size(); i++)
    {
        u64 d = (u64)a[i] - b[i] - borrow;
        a[i] = (u32)d;
        borrow = (d >> 32) & 1;
    }
}

// a * b mod m, by schoolbook multiplication and bit-by-bit reduction: slow, but
// a verification with exponent 65537 needs only 17 of these.
static Num MulMod(const Num& a, const Num& b, const Num& m)
{
    size_t k = m.size();
    std::vector<u32> product(2 * k, 0);
    for (size_t i = 0; i < k; i++)
    {
        u64 carry = 0;
        for (size_t j = 0; j < k; j++)
        {
            u64 t = (u64)a[i] * b[j] + product[i + j] + carry;
            product[i + j] = (u32)t;
            carry = t >> 32;
        }
        product[i + k] = (u32)carry;
    }

    Num r(k, 0);
    for (size_t bit = 2 * k * 32; bit-- > 0;)
    {
        for (size_t i = k; i-- > 1;)
            r[i] = (r[i] << 1) | (r[i - 1] >> 31);
        r[0] = (r[0] << 1) | ((product[bit / 32] >> (bit % 32)) & 1);
        if (Compare(r, m) >= 0)
            SubtractInPlace(r, m);
    }
    return r;
}

bool RsaVerifySha1(const u8* modulus, u32 modulusBytes, u32 exponent, const u8* signature, const u8 hash[20])
{
    static const u8 kSha1DigestInfo[15] =
    {
        0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x05, 0x00, 0x04, 0x14
    };
    if (modulusBytes < 3 + sizeof kSha1DigestInfo + 20 + 8 || modulusBytes > 512 || exponent == 0)
        return false;

    u32 limbs = (modulusBytes + 3) / 4 + 1;
    Num m = FromBigEndian(modulus, modulusBytes, limbs);
    Num s = FromBigEndian(signature, modulusBytes, limbs);
    if (Compare(s, m) >= 0)
        return false;

    Num result(limbs, 0);
    result[0] = 1;
    bool started = false;
    for (int bit = 31; bit >= 0; bit--)
    {
        if (started)
            result = MulMod(result, result, m);
        if ((exponent >> bit) & 1)
        {
            result = started ? MulMod(result, s, m) : s;
            started = true;
        }
    }

    std::vector<u8> message(modulusBytes);
    ToBigEndian(result, &message[0], modulusBytes);
    u32 padEnd = modulusBytes - 20 - sizeof kSha1DigestInfo - 1;
    if (message[0] != 0x00 || message[1] != 0x01 || message[padEnd] != 0x00)
        return false;
    for (u32 i = 2; i < padEnd; i++)
        if (message[i] != 0xFF)
            return false;
    return memcmp(&message[padEnd + 1], kSha1DigestInfo, sizeof kSha1DigestInfo) == 0 &&
           memcmp(&message[padEnd + 1 + sizeof kSha1DigestInfo], hash, 20) == 0;
}
