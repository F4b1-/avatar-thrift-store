#include "account.h"
#include "bytes.h"
#include "sha1.h"

#include <string.h>

static const u8 kRetailAccountKey[16] =
{
    0xE1, 0xBC, 0x15, 0x9C, 0x73, 0xB1, 0xEA, 0xE9, 0xAB, 0x31, 0x70, 0xF3, 0xAD, 0x47, 0xEB, 0xF3
};

static const u8 kDevkitAccountKey[16] =
{
    0xDA, 0xB6, 0x9A, 0xD9, 0x8E, 0x28, 0x76, 0x4F, 0x97, 0x7E, 0xE2, 0x48, 0x7E, 0x4F, 0x3F, 0x68
};

static void Rc4(const u8* key, u32 keySize, u8* data, u32 size)
{
    u8 s[256];
    for (int i = 0; i < 256; i++)
        s[i] = (u8)i;
    for (int i = 0, j = 0; i < 256; i++)
    {
        j = (j + s[i] + key[i % keySize]) & 0xFF;
        u8 t = s[i]; s[i] = s[j]; s[j] = t;
    }
    int i = 0, j = 0;
    for (u32 n = 0; n < size; n++)
    {
        i = (i + 1) & 0xFF;
        j = (j + s[i]) & 0xFF;
        u8 t = s[i]; s[i] = s[j]; s[j] = t;
        data[n] ^= s[(s[i] + s[j]) & 0xFF];
    }
}

static bool TryDecrypt(const u8* file, const u8* key, u8 plain[kAccountFileSize - 0x10])
{
    u8 digest[20];
    HmacSha1(key, 16, file, 0x10, digest);
    memcpy(plain, file + 0x10, kAccountFileSize - 0x10);
    Rc4(digest, 16, plain, kAccountFileSize - 0x10);

    HmacSha1(key, 16, plain, kAccountFileSize - 0x10, digest);
    return memcmp(digest, file, 0x10) == 0;
}

bool DecryptAccount(const u8* file, u32 size, ProfileAccount* out, std::string* error)
{
    if (size < kAccountFileSize)
    {
        if (error) *error = "Account file too small";
        return false;
    }

    u8 plain[kAccountFileSize - 0x10];
    if (TryDecrypt(file, kRetailAccountKey, plain))
        out->keyType = kAccountKeyRetail;
    else if (TryDecrypt(file, kDevkitAccountKey, plain))
        out->keyType = kAccountKeyDevkit;
    else
    {
        if (error) *error = "Account file could not be decrypted (HMAC mismatch)";
        return false;
    }

    const u8* d = plain + 8;   // skip confounder
    memcpy(out->data, d, kAccountDataSize);
    out->reservedFlags = ReadBE32(d + 0x00);
    out->liveFlags = ReadBE32(d + 0x04);
    out->onlineXuid = ReadBE64(d + 0x28);

    out->gamertag.clear();
    for (int i = 0; i < 16; i++)
    {
        u16 c = ReadBE16(d + 0x08 + i * 2);
        if (c == 0)
            break;
        out->gamertag += c < 0x80 ? (char)c : '?';
    }
    return true;
}

void EncryptAccount(const u8 data[kAccountDataSize], const u8 confounder[8], AccountKeyType keyType,
                    u8 out[kAccountFileSize])
{
    const u8* key = keyType == kAccountKeyRetail ? kRetailAccountKey : kDevkitAccountKey;
    u8* body = out + 0x10;
    memcpy(body, confounder, 8);
    memcpy(body + 8, data, kAccountDataSize);

    u8 digest[20];
    HmacSha1(key, 16, body, kAccountFileSize - 0x10, digest);
    memcpy(out, digest, 0x10);

    HmacSha1(key, 16, out, 0x10, digest);
    Rc4(digest, 16, body, kAccountFileSize - 0x10);
}

// Writes `text` (no terminator needed) into an all-zero field of `size` bytes.
static void FillIfEmpty(u8* field, u32 size, const char* text)
{
    for (u32 i = 0; i < size; i++)
        if (field[i] != 0)
            return;
    u32 n = (u32)strlen(text);
    memcpy(field, text, n < size ? n : size);
}

bool AccountSetOnlineXuid(u8* file, u32 size, u64 xuid, bool markLiveEnabled, std::string* error)
{
    ProfileAccount account;
    if (!DecryptAccount(file, size, &account, error))
        return false;

    // The confounder is the first 8 decrypted bytes; recover it to keep it.
    const u8* key = account.keyType == kAccountKeyRetail ? kRetailAccountKey : kDevkitAccountKey;
    u8 plain[kAccountFileSize - 0x10];
    TryDecrypt(file, key, plain);

    WriteBE64(account.data + 0x28, xuid);
    if (markLiveEnabled)
    {
        WriteBE32(account.data + 0x00, account.reservedFlags | kAccountLiveEnabled);
        FillIfEmpty(account.data + 0x34, 4, "PROD");
        FillIfEmpty(account.data + 0x3C, 20, "xbox.com");
        FillIfEmpty(account.data + 0x50, 24, "PASSPORT.NET");
        FillIfEmpty(account.data + 0x68, 16, "\x81");
    }
    EncryptAccount(account.data, plain, account.keyType, file);
    return true;
}
