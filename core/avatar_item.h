// Unlocking avatar award items, using the same method as "Avatar Friday
// Patcher" (HungryBob), established by disassembling afpg.exe v1.0:
//
//   1. write a new title id at 0x360 (header title id) and at 0x3ED (last 4
//      bytes of the 16-byte avatar asset id at 0x3E1)
//   2. write the target profile's XUID as licensee of license entry [1] (0x23C)
//   3. recompute the header SHA-1 at 0x32C
//
// Further XUIDs (profiles that have their own online XUID) go into the free
// entries from [2] on, with entry [1]'s bits and flags. Not the patcher's method:
// whether the console accepts entries past [1] is still to be tested.
//
// The package signature is not touched (modded consoles' kernels don't check it).
// The patched file is named after its (new) asset id and goes to
//   Hdd:\Content\<profile folder>\<TITLEID>\00009000\<ASSETID>
#pragma once

#include "types.h"

#include <string>
#include <vector>

enum
{
    kAvatarOffsetLicensee1  = 0x23C,   // license entry [1], licensee id
    kAvatarOffsetAssetId    = 0x3E1,   // 16 bytes
    kAvatarOffsetAssetTitle = 0x3ED,   // last 4 bytes of the asset id
    kAvatarAssetIdSize      = 16,
};

// Title id used when the caller doesn't choose one (same as the patcher's guide).
static const u32 kAvatarDefaultTitleId = 0xAAAAAAAA;

struct AvatarPatchOptions
{
    u32  newTitleId;
    bool setXuid;
    u64  xuid;
    std::vector<u64> extraXuids;   // added to free entries [2]..[15] unless already licensed
};

// Checks that the buffer holds a complete avatar item header that can be patched.
bool AvatarItemValidate(const u8* data, u32 size, std::string* error);

// Patches the package in place. `data` must hold at least the whole hashed
// header region (normally just pass the entire file).
bool AvatarItemPatch(u8* data, u32 size, const AvatarPatchOptions& options, std::string* error);

// File name of the package: its asset id as 32 uppercase hex digits.
std::string AvatarItemFileName(const u8* data);

// The file name the package gets once patched to `titleId` (its asset id ends
// in the title id), without patching it.
std::string AvatarItemPatchedFileName(const u8* data, u32 titleId);

// True when one of the package's license entries names `xuid`.
bool AvatarItemIsLicensedTo(const u8* data, u64 xuid);

// Content folder for patched items below a profile's content folder: "AAAAAAAA\00009000".
std::string AvatarItemContentSubfolder(u32 titleId);

bool ParseHex32(const char* text, u32* out);
bool ParseHex64(const char* text, u64* out);
