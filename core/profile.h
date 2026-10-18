// Profile packages (CON, content type 0x10000): inspecting them, giving an
// offline profile the shared online XUID, and applying a console signature.
// Shared by avtool (PC) and the console app.
#pragma once

#include "account.h"
#include "stfs.h"
#include "types.h"

#include <string>
#include <vector>

enum ProfileKind
{
    kProfileOffline,     // no online XUID, not Live-flagged
    kProfileActivated,   // Live-flagged with kSharedOnlineXuid (this tool's result)
    kProfileLive,        // a real Live profile, or some other online XUID
};

struct ProfileInfo
{
    std::string gamertag;
    u64         offlineXuid;    // the header's profile id, also the folder name
    u64         onlineXuid;
    u32         reservedFlags;
    ProfileKind kind;
    bool        signatureValid; // console signature matches its certificate
};

// Reads a whole profile package; fails for anything but a readable CON profile.
bool ProfileInspect(const u8* data, u32 size, ProfileInfo* out, std::string* error);

// Rewrites the Account file with `xuid` (and, with `markLive`, Live-flagged as
// AccountSetOnlineXuid describes), rehashes the package and its header and
// checks the result from scratch. The console signature is left as it was:
// sign afterwards with ProfileApplyConsoleSignature.
bool ProfileSetOnlineXuid(u8* data, u32 size, u64 xuid, bool markLive, std::string* error);

// Writes a console signature, the 0x228 bytes XeKeysConsolePrivateKeySign
// returns for StfsComputeSignedHash's hash, at 0x004, and refuses (leaving the
// data as it was) unless it verifies against its own certificate.
bool ProfileApplyConsoleSignature(u8* data, u32 size, const u8* signature, std::string* error);

// The avatar: the 1000-byte description in setting 0x63E80044 of the
// profile's dashboard GPD (see sona.h). A profile whose avatar was never made
// has no such setting, and both calls fail for it.
bool ProfileReadAvatar(const u8* data, u32 size, std::vector<u8>* manifest, std::string* error);

// Replaces the avatar in place, rehashes the package and its header and
// checks the result from scratch, as ProfileSetOnlineXuid does. The console
// signature is left as it was: sign afterwards with ProfileApplyConsoleSignature.
bool ProfileWriteAvatar(u8* data, u32 size, const u8* manifest, std::string* error);
