// The "Account" file inside a profile package: gamertag, online XUID and
// Live flags, encrypted with RC4 under a key derived via HMAC-SHA1.
//
// Encrypted layout (0x194 bytes): HMAC (0x10) | confounder (8) | data (0x17C)
//   rc4 key = HMAC-SHA1(accountKey, HMAC)[0..16], decrypts confounder + data
//   valid  <=> HMAC-SHA1(accountKey, confounder + data)[0..16] == HMAC
// Decrypted data:
//   0x00 reserved flags (u32)   0x04 live flags (u32)
//   0x08 gamertag, 16 UTF-16BE chars
//   0x28 online XUID (u64; 0 for offline-only profiles)
//   Live profiles also have (all empty in offline-only ones):
//   0x30 cached user flags (u32)    0x34 service id, 4 chars ("PROD")
//   0x3C online domain ("xbox.com") 0x50 Kerberos realm ("PASSPORT.NET")
//   0x68 16 bytes of the Live account's key material; 360mpGui's offline-to-
//        online maker writes only 0x81 at 0x68, and with all zeros there the
//        console refuses a Live-flagged profile
#pragma once

#include "types.h"

#include <string>

enum
{
    kAccountFileSize = 0x194,
    kAccountDataSize = 0x17C,
};

// Online XUID given to offline profiles, the same for everyone so item
// packages patched for it work with every such profile. Online form (0009...),
// far above the range Live hands out, and easy to spot like the AAAAAAAA title.
static const u64 kSharedOnlineXuid = 0x00090000AAAAAAAAull;

// Online XUIDs (Live accounts, and the shared one) normally start with 0009.
// Accepted is anything but zero and the offline/local range (E.../F...,
// which includes the "everyone" license FFFF...).
inline bool IsOnlineXuid(u64 xuid)
{
    u32 top = (u32)(xuid >> 60);
    return xuid != 0 && top != 0xE && top != 0xF;
}

enum AccountKeyType
{
    kAccountKeyRetail,
    kAccountKeyDevkit,
};

struct ProfileAccount
{
    AccountKeyType keyType;
    u32            reservedFlags;
    u32            liveFlags;
    std::string    gamertag;     // UTF-8
    u64            onlineXuid;   // 0x0009...; 0 if the profile was never Live-enabled
    u8             data[kAccountDataSize];
};

// Decrypts and verifies the Account file (tries the retail key, then devkit).
bool DecryptAccount(const u8* file, u32 size, ProfileAccount* out, std::string* error);

// Encrypts `data` with `confounder` under the given key into a full Account file.
void EncryptAccount(const u8 data[kAccountDataSize], const u8 confounder[8], AccountKeyType keyType,
                    u8 out[kAccountFileSize]);

// Reserved flags bit that Live profiles have (a Live profile 0x20000001, an
// offline-only one 0x00000001).
static const u32 kAccountLiveEnabled = 0x20000000;

// Rewrites the online XUID (data 0x28) in place, keeping the key type, the
// confounder and every other field. With `markLiveEnabled` it also sets
// kAccountLiveEnabled in the reserved flags and, where they're empty, fills
// the generic Live fields (service id, domain, realm) and the 0x81 at 0x68,
// as 360mpGui's Account template has them; Horizon refuses a Live profile
// without a service id. Fields that are already set stay as they are.
bool AccountSetOnlineXuid(u8* file, u32 size, u64 xuid, bool markLiveEnabled, std::string* error);
