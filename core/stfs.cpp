#include "stfs.h"
#include "bytes.h"
#include "rsa.h"
#include "sha1.h"

#include <string.h>

// Converts a zero-terminated UTF-16BE field of at most `maxBytes` to UTF-8.
static std::string Utf16BeToUtf8(const u8* p, u32 maxBytes)
{
    std::string out;
    for (u32 i = 0; i + 1 < maxBytes; i += 2)
    {
        u32 c = ReadBE16(p + i);
        if (c == 0)
            break;

        // Surrogate pair
        if (c >= 0xD800 && c <= 0xDBFF && i + 3 < maxBytes)
        {
            u32 low = ReadBE16(p + i + 2);
            if (low >= 0xDC00 && low <= 0xDFFF)
            {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }

        if (c < 0x80)
        {
            out += (char)c;
        }
        else if (c < 0x800)
        {
            out += (char)(0xC0 | (c >> 6));
            out += (char)(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            out += (char)(0xE0 | (c >> 12));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        }
        else
        {
            out += (char)(0xF0 | (c >> 18));
            out += (char)(0x80 | ((c >> 12) & 0x3F));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

static StfsMagic ParseMagic(const u8* p)
{
    if (memcmp(p, "CON ", 4) == 0) return kStfsMagicCON;
    if (memcmp(p, "LIVE", 4) == 0) return kStfsMagicLIVE;
    if (memcmp(p, "PIRS", 4) == 0) return kStfsMagicPIRS;
    return kStfsMagicUnknown;
}

u32 StfsHashedHeaderEnd(u32 headerSize)
{
    return (headerSize + 0xFFF) & ~0xFFFu;
}

bool StfsComputeHeaderHash(const u8* data, u32 size, u32 headerSize, u8 out[20])
{
    u32 end = StfsHashedHeaderEnd(headerSize);
    if (end <= kStfsOffsetContentType || end > size)
        return false;
    Sha1(data + kStfsOffsetContentType, end - kStfsOffsetContentType, out);
    return true;
}

bool StfsParseHeader(const u8* data, u32 size, StfsHeader* out, std::string* error)
{
    if (size < kStfsOffsetTitleNameEnd)
    {
        if (error) *error = "file too small for an STFS header";
        return false;
    }

    out->magic = ParseMagic(data + kStfsOffsetMagic);
    if (out->magic == kStfsMagicUnknown)
    {
        if (error) *error = "not an STFS package (magic is not CON/LIVE/PIRS)";
        return false;
    }

    for (int i = 0; i < kStfsLicenseCount; i++)
    {
        const u8* e = data + kStfsOffsetLicenses + i * 16;
        out->licenses[i].licenseeId = ReadBE64(e);
        out->licenses[i].bits = ReadBE32(e + 8);
        out->licenses[i].flags = ReadBE32(e + 12);
    }

    memcpy(out->headerHash, data + kStfsOffsetHeaderHash, 20);
    out->headerSize      = ReadBE32(data + kStfsOffsetHeaderSize);
    out->contentType     = ReadBE32(data + kStfsOffsetContentType);
    out->metadataVersion = ReadBE32(data + kStfsOffsetMetaVersion);
    out->contentSize     = ReadBE64(data + kStfsOffsetContentSize);
    out->titleId         = ReadBE32(data + kStfsOffsetTitleId);
    memcpy(out->consoleId, data + kStfsOffsetConsoleId, 5);
    out->profileId       = ReadBE64(data + kStfsOffsetProfileId);
    memcpy(out->deviceId, data + kStfsOffsetDeviceId, 20);
    out->displayName     = Utf16BeToUtf8(data + kStfsOffsetDisplayName, 0x80);
    out->titleName       = Utf16BeToUtf8(data + kStfsOffsetTitleName, 0x80);

    u8 computed[20];
    out->headerHashChecked = StfsComputeHeaderHash(data, size, out->headerSize, computed);
    out->headerHashValid = out->headerHashChecked && memcmp(computed, out->headerHash, 20) == 0;

    return true;
}

void StfsComputeSignedHash(const u8* data, u8 out[20])
{
    Sha1(data + kStfsOffsetSignedData, kStfsSignedDataSize, out);
}

bool StfsVerifyConSignature(const u8* data, u32 size)
{
    if (size < kStfsOffsetSignedData + kStfsSignedDataSize || ParseMagic(data) != kStfsMagicCON)
        return false;

    // The modulus is stored with its 8-byte blocks in reverse order, the
    // signature fully byte-reversed.
    u8 modulus[kStfsRsaBytes];
    u8 signature[kStfsRsaBytes];
    for (u32 block = 0; block < kStfsRsaBytes / 8; block++)
        memcpy(modulus + block * 8, data + kStfsOffsetConsoleModulus + kStfsRsaBytes - 8 - block * 8, 8);
    for (u32 i = 0; i < kStfsRsaBytes; i++)
        signature[i] = data[kStfsOffsetConsoleSigBytes + kStfsRsaBytes - 1 - i];

    u8 hash[20];
    StfsComputeSignedHash(data, hash);
    return RsaVerifySha1(modulus, kStfsRsaBytes, ReadBE32(data + kStfsOffsetConsoleExponent), signature, hash);
}

bool StfsThumbnail(const u8* data, u32 size, const u8** png, u32* pngBytes)
{
    if (size < kStfsOffsetThumbnail)
        return false;
    u32 maxBytes = ReadBE32(data + kStfsOffsetMetaVersion) >= 2 ? 0x3D00 : 0x4000;
    u32 bytes = ReadBE32(data + kStfsOffsetThumbnailSize);
    if (bytes == 0 || bytes > maxBytes || kStfsOffsetThumbnail + bytes > size)
        return false;
    *png = data + kStfsOffsetThumbnail;
    *pngBytes = bytes;
    return true;
}

const char* StfsMagicName(StfsMagic magic)
{
    switch (magic)
    {
    case kStfsMagicCON:  return "CON";
    case kStfsMagicLIVE: return "LIVE";
    case kStfsMagicPIRS: return "PIRS";
    default:             return "unknown";
    }
}

const char* StfsContentTypeName(u32 contentType)
{
    switch (contentType)
    {
    case 0x00000001: return "Saved Game";
    case kContentTypeMarketplace: return "Marketplace Content";
    case 0x00000003: return "Publisher";
    case 0x00001000: return "IPTV DVR";
    case 0x00002000: return "Installed Game";
    case 0x00004000: return "Installer";
    case 0x00007000: return "Game on Demand";
    case 0x00008000: return "Avatar Asset Pack";
    case kContentTypeAvatarItem: return "Avatar Item";
    case kContentTypeProfile: return "Profile";
    case 0x00020000: return "Gamer Picture";
    case 0x00030000: return "Theme";
    case 0x00040000: return "Cache File";
    case 0x00050000: return "Storage Download";
    case 0x00060000: return "Xbox Saved Game";
    case 0x00070000: return "Xbox Download";
    case 0x00080000: return "Game Demo";
    case 0x00090000: return "Video";
    case 0x000A0000: return "Game Title";
    case 0x000B0000: return "Installer";
    case 0x000C0000: return "Game Trailer";
    case 0x000D0000: return "Arcade Title";
    case 0x000E0000: return "XNA";
    case 0x000F0000: return "License Store";
    case 0x00100000: return "Movie";
    case 0x00200000: return "TV";
    case 0x00300000: return "Music Video";
    case 0x00400000: return "Game Video";
    case 0x00500000: return "Podcast Video";
    case 0x00600000: return "Viral Video";
    case 0x02000000: return "Community Game";
    default:         return "unknown";
    }
}

std::string HexString(const u8* data, u32 size)
{
    static const char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(size * 2);
    for (u32 i = 0; i < size; i++)
    {
        out += kDigits[data[i] >> 4];
        out += kDigits[data[i] & 0xF];
    }
    return out;
}
