#include "avatar_item.h"
#include "bytes.h"
#include "stfs.h"

#include <stdio.h>
#include <string.h>

bool AvatarItemValidate(const u8* data, u32 size, std::string* error)
{
    StfsHeader h;
    if (!StfsParseHeader(data, size, &h, error))
        return false;

    if (h.contentType != kContentTypeAvatarItem)
    {
        if (error) *error = std::string("not an avatar item (content type: ") + StfsContentTypeName(h.contentType) + ")";
        return false;
    }
    if (!h.headerHashChecked)
    {
        if (error) *error = "file too small, header incomplete";
        return false;
    }
    return true;
}

bool AvatarItemPatch(u8* data, u32 size, const AvatarPatchOptions& options, std::string* error)
{
    if (!AvatarItemValidate(data, size, error))
        return false;

    WriteBE32(data + kStfsOffsetTitleId, options.newTitleId);
    WriteBE32(data + kAvatarOffsetAssetTitle, options.newTitleId);
    if (options.setXuid)
        WriteBE64(data + kAvatarOffsetLicensee1, options.xuid);
    for (size_t i = 0; i < options.extraXuids.size(); i++)
    {
        u64 xuid = options.extraXuids[i];
        if (xuid == 0 || AvatarItemIsLicensedTo(data, xuid))
            continue;
        int free = 2;
        while (free < kStfsLicenseCount && ReadBE64(data + kStfsOffsetLicenses + free * 16) != 0)
            free++;
        if (free == kStfsLicenseCount)
        {
            if (error) *error = "no free license entry left";
            return false;
        }
        u8* entry = data + kStfsOffsetLicenses + free * 16;
        WriteBE64(entry, xuid);
        memcpy(entry + 8, data + kStfsOffsetLicenses + 16 + 8, 8);   // bits and flags of entry [1]
    }

    u32 headerSize = ReadBE32(data + kStfsOffsetHeaderSize);
    u8 hash[20];
    if (!StfsComputeHeaderHash(data, size, headerSize, hash))
    {
        if (error) *error = "cannot compute header hash";
        return false;
    }
    memcpy(data + kStfsOffsetHeaderHash, hash, 20);
    return true;
}

std::string AvatarItemFileName(const u8* data)
{
    return HexString(data + kAvatarOffsetAssetId, kAvatarAssetIdSize);
}

std::string AvatarItemPatchedFileName(const u8* data, u32 titleId)
{
    u8 assetId[kAvatarAssetIdSize];
    memcpy(assetId, data + kAvatarOffsetAssetId, kAvatarAssetIdSize);
    WriteBE32(assetId + kAvatarAssetIdSize - 4, titleId);
    return HexString(assetId, kAvatarAssetIdSize);
}

AvatarBodyType AvatarItemBodyType(const u8* data)
{
    u32 type = data[kAvatarOffsetAssetBody] & 0x0F;
    return type <= kAvatarBodyBoth ? (AvatarBodyType)type : kAvatarBodyUnknown;
}

const char* AvatarBodyTypeName(AvatarBodyType type)
{
    switch (type)
    {
    case kAvatarBodyMale:   return "male";
    case kAvatarBodyFemale: return "female";
    case kAvatarBodyBoth:   return "both";
    default:                return "unknown";
    }
}

bool AvatarItemIsLicensedTo(const u8* data, u64 xuid)
{
    for (int i = 0; i < kStfsLicenseCount; i++)
        if (ReadBE64(data + kStfsOffsetLicenses + i * 16) == xuid)
            return true;
    return false;
}

std::string AvatarItemContentSubfolder(u32 titleId)
{
    char buf[32];
    sprintf(buf, "%08X\\00009000", titleId);
    return buf;
}

static bool ParseHex(const char* text, int digits, u64* out)
{
    if (!text || (int)strlen(text) != digits)
        return false;
    u64 v = 0;
    for (int i = 0; i < digits; i++)
    {
        char c = text[i];
        int d;
        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | (u64)d;
    }
    *out = v;
    return true;
}

bool ParseHex32(const char* text, u32* out)
{
    u64 v;
    if (!ParseHex(text, 8, &v))
        return false;
    *out = (u32)v;
    return true;
}

bool ParseHex64(const char* text, u64* out)
{
    return ParseHex(text, 16, out);
}
