// Declarations for the imports in imports.txt. xsdk's GCC calls them by the
// console's own convention, so they are declared as the kernel exports them.
#pragma once

#include <stdint.h>

extern "C"
{

// Signs a 20-byte SHA-1 hash with the console's private key and returns the
// console certificate (0x1A8 bytes) followed by the signature (0x80 bytes),
// laid out as a CON header holds them at 0x004 (docs/STATUS.md). The return
// value's meaning is unconfirmed (assumed BOOL); callers verify the output instead.
uint32_t XeKeysConsolePrivateKeySign(const uint8_t* hash, uint8_t* consoleSignature);

// XUSER_SIGNIN_INFO (xenia:src/xenia/kernel/xam/xam_user.cc).
struct XUserSigninInfo
{
    uint64_t xuid;
    uint32_t flags;
    uint32_t signinState;   // 0 = not signed in
    uint32_t guestNumber;
    uint32_t sponsorUserIndex;
    char     name[16];
};

enum
{
    // Asks for the offline XUID, the one profiles are named after.
    XUSER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY = 1,
    // Asks for the online XUID (0 when the profile has none).
    XUSER_GET_SIGNIN_INFO_ONLINE_XUID_ONLY = 2,
};

// 0 (ERROR_SUCCESS) when user_index (0-3) is signed in; fills `info`.
uint32_t XamUserGetSigninInfo(uint32_t userIndex, uint32_t flags, XUserSigninInfo* info);

}
