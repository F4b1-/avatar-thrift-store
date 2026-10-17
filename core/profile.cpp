#include "profile.h"
#include "bytes.h"
#include "stfs_package.h"

#include <string.h>
#include <vector>

static bool ReadAccount(const StfsPackage& pkg, ProfileAccount* account, std::string* error)
{
    const StfsFileEntry* entry = pkg.FindFile("Account");
    std::vector<u8> file;
    if (!entry || !pkg.ReadFile(*entry, &file, error))
    {
        if (error && error->empty()) *error = "no Account file";
        return false;
    }
    return DecryptAccount(&file[0], (u32)file.size(), account, error);
}

static bool OpenProfile(const u8* data, u32 size, StfsHeader* header, StfsPackage* pkg, std::string* error)
{
    if (!StfsParseHeader(data, size, header, error))
        return false;
    if (header->magic != kStfsMagicCON || header->contentType != kContentTypeProfile)
    {
        if (error) *error = "not a profile";
        return false;
    }
    return pkg->Open(data, size, error);
}

bool ProfileInspect(const u8* data, u32 size, ProfileInfo* out, std::string* error)
{
    StfsHeader header;
    StfsPackage pkg;
    ProfileAccount account;
    if (!OpenProfile(data, size, &header, &pkg, error) || !ReadAccount(pkg, &account, error))
        return false;

    out->gamertag = account.gamertag;
    out->offlineXuid = header.profileId;
    out->onlineXuid = account.onlineXuid;
    out->reservedFlags = account.reservedFlags;
    bool live = (account.reservedFlags & kAccountLiveEnabled) != 0;
    if (!live && account.onlineXuid == 0)
        out->kind = kProfileOffline;
    else if (live && account.onlineXuid == kSharedOnlineXuid)
        out->kind = kProfileActivated;
    else
        out->kind = kProfileLive;
    out->signatureValid = StfsVerifyConSignature(data, size);
    return true;
}

bool ProfileSetOnlineXuid(u8* data, u32 size, u64 xuid, bool markLive, std::string* error)
{
    StfsHeader header;
    StfsPackage pkg;
    if (!OpenProfile(data, size, &header, &pkg, error))
        return false;

    const StfsFileEntry* entry = pkg.FindFile("Account");
    std::vector<u8> account;
    if (!entry || !pkg.ReadFile(*entry, &account, error))
    {
        if (error && error->empty()) *error = "no Account file";
        return false;
    }
    if (!AccountSetOnlineXuid(&account[0], (u32)account.size(), xuid, markLive, error) ||
        !pkg.ReplaceFileData(data, *entry, &account[0], (u32)account.size(), error))
        return false;

    // The volume descriptor's top hash lies inside the hashed header region.
    StfsComputeHeaderHash(data, size, header.headerSize, data + kStfsOffsetHeaderHash);

    // Check the result from scratch.
    StfsPackage check;
    ProfileAccount result;
    u32 blocks = 0;
    u32 tables = 0;
    if (!check.Open(data, size, error) || check.VerifyAllBlocks(&blocks) != 0 || check.VerifyHashTables(&tables) != 0 ||
        !StfsParseHeader(data, size, &header, error) || !header.headerHashValid ||
        !ReadAccount(check, &result, error) || result.onlineXuid != xuid ||
        (markLive && !(result.reservedFlags & kAccountLiveEnabled)))
    {
        if (error) *error = "the rewritten profile failed verification";
        return false;
    }
    return true;
}

bool ProfileApplyConsoleSignature(u8* data, u32 size, const u8* signature, std::string* error)
{
    if (size < kStfsOffsetSignedData + kStfsSignedDataSize || ReadBE32(data) != 0x434F4E20)   // "CON "
    {
        if (error) *error = "not a CON package";
        return false;
    }
    u8 previous[kStfsConsoleSignatureSize];
    memcpy(previous, data + kStfsOffsetConsoleSignature, sizeof previous);
    memcpy(data + kStfsOffsetConsoleSignature, signature, kStfsConsoleSignatureSize);
    if (!StfsVerifyConSignature(data, size))
    {
        memcpy(data + kStfsOffsetConsoleSignature, previous, sizeof previous);
        if (error) *error = "the console signature does not verify";
        return false;
    }
    return true;
}
