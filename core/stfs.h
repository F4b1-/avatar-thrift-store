// STFS package header (CON / LIVE / PIRS). Avatar items, profiles, DLC and
// saves all use this container format.
//
// Layout reference (offsets from the start of the file, all big-endian):
//   0x000  magic "CON " / "LIVE" / "PIRS"
//   0x004  signature (CON: console certificate + signature, LIVE/PIRS: RSA sig)
//          signed: SHA-1 of [0x22C, 0x344), RSA PKCS#1 v1.5
//          CON: certificate 0x1A8 bytes (public exponent at file 0x28, modulus
//          at 0x2C with its 8-byte blocks reversed), signature at 0x1AC,
//          byte-reversed (X360.dll's STFSPackage/ScrambleMethods)
//   0x22C  16 license entries (u64 licensee id, u32 license bits, u32 flags)
//   0x32C  header hash: SHA-1 over [0x344, align4k(headerSize))  (= content id)
//   0x340  header size
//   0x344  content type, 0x348 metadata version, 0x34C content size
//   0x360  title id, 0x36C console id (5 bytes), 0x371 profile id (u64)
//   0x379  STFS volume descriptor
//   0x3FD  device id (20 bytes)
//   0x411  display names, 18 locales x 0x80 bytes, UTF-16BE (English first)
//   0x1691 title name, 0x80 bytes, UTF-16BE
//   0x1712 thumbnail size, 0x171A thumbnail PNG (at most 0x4000 bytes, 0x3D00
//          from metadata version 2)
#pragma once

#include "types.h"

#include <string>

enum
{
    kStfsLicenseCount   = 16,

    kStfsOffsetMagic        = 0x000,
    kStfsOffsetConsoleSignature = 0x004,
    kStfsConsoleSignatureSize   = 0x228,   // certificate 0x1A8 + signature 0x80
    kStfsOffsetConsoleExponent  = 0x028,
    kStfsOffsetConsoleModulus   = 0x02C,
    kStfsOffsetConsoleSigBytes  = 0x1AC,
    kStfsRsaBytes               = 0x80,
    kStfsOffsetSignedData   = 0x22C,
    kStfsSignedDataSize     = 0x118,
    kStfsOffsetLicenses     = 0x22C,
    kStfsOffsetHeaderHash   = 0x32C,
    kStfsOffsetHeaderSize   = 0x340,
    kStfsOffsetContentType  = 0x344,
    kStfsOffsetMetaVersion  = 0x348,
    kStfsOffsetContentSize  = 0x34C,
    kStfsOffsetTitleId      = 0x360,
    kStfsOffsetConsoleId    = 0x36C,
    kStfsOffsetProfileId    = 0x371,
    kStfsOffsetDeviceId     = 0x3FD,
    kStfsOffsetDisplayName  = 0x411,
    kStfsOffsetTitleName    = 0x1691,
    kStfsOffsetTitleNameEnd = 0x1711,
    kStfsOffsetThumbnailSize = 0x1712,
    kStfsOffsetThumbnail     = 0x171A,

    // Reading this many bytes always covers the whole hashed header region.
    kStfsMaxHeaderRead      = 0xB000,
};

enum StfsMagic
{
    kStfsMagicUnknown = 0,
    kStfsMagicCON,
    kStfsMagicLIVE,
    kStfsMagicPIRS,
};

enum StfsContentType
{
    kContentTypeMarketplace = 0x00000002,
    kContentTypeAvatarItem  = 0x00009000,
    kContentTypeProfile     = 0x00010000,
};

struct StfsLicense
{
    u64 licenseeId;
    u32 bits;
    u32 flags;
};

struct StfsHeader
{
    StfsMagic   magic;
    StfsLicense licenses[kStfsLicenseCount];
    u8          headerHash[20];
    u32         headerSize;
    u32         contentType;
    u32         metadataVersion;
    u64         contentSize;
    u32         titleId;
    u8          consoleId[5];
    u64         profileId;
    u8          deviceId[20];
    std::string displayName;   // UTF-8, English locale
    std::string titleName;     // UTF-8

    // True when the buffer covered the full hashed region and the stored
    // header hash matched the recomputed one.
    bool        headerHashChecked;
    bool        headerHashValid;
};

// Parses the header from the first bytes of a package. Pass at least
// kStfsMaxHeaderRead bytes (or the whole file if smaller) so the header hash
// can be verified. Returns false and fills `error` on failure.
bool StfsParseHeader(const u8* data, u32 size, StfsHeader* out, std::string* error);

// End offset (exclusive) of the region covered by the header hash.
u32 StfsHashedHeaderEnd(u32 headerSize);

// Recomputes the header hash. `size` must be >= StfsHashedHeaderEnd(headerSize).
bool StfsComputeHeaderHash(const u8* data, u32 size, u32 headerSize, u8 out[20]);

// SHA-1 of the signed header region [0x22C, 0x344). `data` must cover it.
void StfsComputeSignedHash(const u8* data, u8 out[20]);

// Checks a CON package's console signature against the public key in its own
// certificate. `size` must cover the header up to 0x344.
bool StfsVerifyConSignature(const u8* data, u32 size);

// The header's thumbnail PNG, or false when there is none.
bool StfsThumbnail(const u8* data, u32 size, const u8** png, u32* pngBytes);

const char* StfsMagicName(StfsMagic magic);
const char* StfsContentTypeName(u32 contentType);

std::string HexString(const u8* data, u32 size);
