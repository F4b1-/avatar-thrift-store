// Unit tests for the core library (PC only). Minimal harness, no dependencies.

#include "../core/account.h"
#include "../core/avatar_item.h"
#include "../core/bytes.h"
#include "../core/profile.h"
#include "../core/rsa.h"
#include "../core/sha1.h"
#include "../core/stfs.h"
#include "../core/stfs_package.h"

#include <stdio.h>
#include <string.h>
#include <vector>

static int g_failures = 0;
static const char* g_context = NULL;   // the case a loop is on, printed with failures

#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s%s%s%s\n", __FILE__, __LINE__, #cond, g_context ? "  [" : "", \
                               g_context ? g_context : "", g_context ? "]" : ""); g_failures++; } } while (0)

static std::string Sha1Hex(const char* s)
{
    u8 d[20];
    Sha1(s, (u32)strlen(s), d);
    return HexString(d, 20);
}

static void TestSha1()
{
    CHECK(Sha1Hex("") == "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709");
    CHECK(Sha1Hex("abc") == "A9993E364706816ABA3E25717850C26C9CD0D89D");
    CHECK(Sha1Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "84983E441C3BD26EBAAE4AA1F95129E5E54670F1");

    // Streaming in odd-sized chunks must match one-shot hashing.
    std::vector<u8> big(100000);
    for (size_t i = 0; i < big.size(); i++)
        big[i] = (u8)(i * 31 + 7);
    u8 oneShot[20], streamed[20];
    Sha1(&big[0], (u32)big.size(), oneShot);
    Sha1Context ctx;
    Sha1Init(&ctx);
    for (size_t pos = 0; pos < big.size(); pos += 777)
    {
        u32 n = (u32)(big.size() - pos < 777 ? big.size() - pos : 777);
        Sha1Update(&ctx, &big[pos], n);
    }
    Sha1Final(&ctx, streamed);
    CHECK(memcmp(oneShot, streamed, 20) == 0);
}

static void WriteUtf16Be(u8* p, const char* ascii)
{
    for (; *ascii; ascii++, p += 2)
    {
        p[0] = 0;
        p[1] = (u8)*ascii;
    }
}

// Builds a synthetic LIVE avatar item header with a correct header hash.
static std::vector<u8> MakeSyntheticPackage()
{
    std::vector<u8> pkg(kStfsMaxHeaderRead, 0);
    u8* p = &pkg[0];
    memcpy(p, "LIVE", 4);
    WriteBE64(p + kStfsOffsetLicenses, 0xFFFFFFFFFFFFFFFFull);
    WriteBE32(p + kStfsOffsetLicenses + 8, 0x00000001);
    WriteBE32(p + kStfsOffsetHeaderSize, 0x971A);
    WriteBE32(p + kStfsOffsetContentType, kContentTypeAvatarItem);
    WriteBE32(p + kStfsOffsetMetaVersion, 2);
    WriteBE32(p + kStfsOffsetTitleId, 0x4D5307E6);
    WriteBE64(p + kStfsOffsetProfileId, 0xE000123456789ABCull);
    WriteUtf16Be(p + kStfsOffsetDisplayName, "Test Shirt");
    WriteUtf16Be(p + kStfsOffsetTitleName, "Test Game");

    u8 hash[20];
    StfsComputeHeaderHash(p, (u32)pkg.size(), 0x971A, hash);
    memcpy(p + kStfsOffsetHeaderHash, hash, 20);
    return pkg;
}

static void TestStfsHeader()
{
    std::vector<u8> pkg = MakeSyntheticPackage();
    StfsHeader h;
    std::string error;

    CHECK(StfsParseHeader(&pkg[0], (u32)pkg.size(), &h, &error));
    CHECK(h.magic == kStfsMagicLIVE);
    CHECK(h.contentType == kContentTypeAvatarItem);
    CHECK(h.titleId == 0x4D5307E6);
    CHECK(h.profileId == 0xE000123456789ABCull);
    CHECK(h.displayName == "Test Shirt");
    CHECK(h.titleName == "Test Game");
    CHECK(h.licenses[0].licenseeId == 0xFFFFFFFFFFFFFFFFull);
    CHECK(h.licenses[0].bits == 1);
    CHECK(h.headerSize == 0x971A);
    CHECK(StfsHashedHeaderEnd(h.headerSize) == 0xA000);
    CHECK(h.headerHashChecked && h.headerHashValid);

    // Changing a hashed field (profile id) must invalidate the header hash...
    pkg[kStfsOffsetProfileId + 7] ^= 1;
    CHECK(StfsParseHeader(&pkg[0], (u32)pkg.size(), &h, &error));
    CHECK(h.headerHashChecked && !h.headerHashValid);
    pkg[kStfsOffsetProfileId + 7] ^= 1;

    // ...while the license block lies outside the hashed region.
    pkg[kStfsOffsetLicenses + 3] ^= 1;
    CHECK(StfsParseHeader(&pkg[0], (u32)pkg.size(), &h, &error));
    CHECK(h.headerHashValid);

    // Truncated buffer: parses, but the hash cannot be checked.
    CHECK(StfsParseHeader(&pkg[0], 0x2000, &h, &error));
    CHECK(!h.headerHashChecked);

    // Garbage is rejected.
    memcpy(&pkg[0], "XXXX", 4);
    CHECK(!StfsParseHeader(&pkg[0], (u32)pkg.size(), &h, &error));
    CHECK(!StfsParseHeader(&pkg[0], 16, &h, &error));
}

static void TestAvatarPatch()
{
    std::vector<u8> pkg = MakeSyntheticPackage();
    u8* p = &pkg[0];

    // Asset id ending in the original title id, like real award items.
    static const u8 kAssetId[16] = { 0x00, 0x00, 0x00, 0x08, 0xA2, 0x12, 0xA2, 0x31,
                                     0xC0, 0x5F, 0x8A, 0x70, 0x4D, 0x53, 0x07, 0xE6 };
    memcpy(p + kAvatarOffsetAssetId, kAssetId, 16);
    WriteBE64(p + kAvatarOffsetLicensee1, 0x0009000000000000ull);
    std::vector<u8> original = pkg;

    AvatarPatchOptions opt;
    opt.newTitleId = 0xAAAAAAAA;
    opt.setXuid = true;
    opt.xuid = 0x0009000087654321ull;
    std::string error;
    CHECK(AvatarItemPatch(p, (u32)pkg.size(), opt, &error));

    StfsHeader h;
    CHECK(StfsParseHeader(p, (u32)pkg.size(), &h, &error));
    CHECK(h.titleId == 0xAAAAAAAA);
    CHECK(h.licenses[1].licenseeId == 0x0009000087654321ull);
    CHECK(h.licenses[0].licenseeId == 0xFFFFFFFFFFFFFFFFull);   // untouched
    CHECK(h.headerHashValid);
    CHECK(AvatarItemFileName(p) == "00000008A212A231C05F8A70AAAAAAAA");
    CHECK(AvatarItemContentSubfolder(0xAAAAAAAA) == "AAAAAAAA\\00009000");

    // Only the documented bytes may change.
    for (u32 i = 0; i < pkg.size(); i++)
    {
        bool allowed = (i >= kStfsOffsetTitleId && i < kStfsOffsetTitleId + 4) ||
                       (i >= kAvatarOffsetAssetTitle && i < kAvatarOffsetAssetTitle + 4) ||
                       (i >= kAvatarOffsetLicensee1 && i < kAvatarOffsetLicensee1 + 8) ||
                       (i >= kStfsOffsetHeaderHash && i < kStfsOffsetHeaderHash + 20);
        if (!allowed && pkg[i] != original[i])
        {
            CHECK(!"unexpected byte changed");
            break;
        }
    }

    // Without --xuid the license stays as it was.
    pkg = original;
    opt.setXuid = false;
    CHECK(AvatarItemPatch(&pkg[0], (u32)pkg.size(), opt, &error));
    CHECK(StfsParseHeader(&pkg[0], (u32)pkg.size(), &h, &error));
    CHECK(h.licenses[1].licenseeId == 0x0009000000000000ull);

    // Extra XUIDs go into the free entries from [2], once each, with entry [1]'s bits and flags.
    pkg = original;
    WriteBE32(&pkg[kStfsOffsetLicenses + 16 + 8], 0x00000000);
    WriteBE32(&pkg[kStfsOffsetLicenses + 16 + 12], 0x00000001);
    opt.setXuid = true;
    opt.xuid = kSharedOnlineXuid;
    opt.extraXuids.push_back(0x0009000087654321ull);
    opt.extraXuids.push_back(kSharedOnlineXuid);       // already entry [1]
    opt.extraXuids.push_back(0x0009000087654321ull);   // duplicate
    opt.extraXuids.push_back(0);
    CHECK(AvatarItemPatch(&pkg[0], (u32)pkg.size(), opt, &error));
    CHECK(AvatarItemPatch(&pkg[0], (u32)pkg.size(), opt, &error));   // patching again changes nothing
    CHECK(StfsParseHeader(&pkg[0], (u32)pkg.size(), &h, &error));
    CHECK(h.licenses[1].licenseeId == kSharedOnlineXuid);
    CHECK(h.licenses[2].licenseeId == 0x0009000087654321ull);
    CHECK(h.licenses[2].bits == 0 && h.licenses[2].flags == 1);
    CHECK(h.licenses[3].licenseeId == 0);
    CHECK(h.headerHashValid);

    // Only 14 extra entries fit.
    opt.extraXuids.clear();
    for (u64 i = 0; i < 15; i++)
        opt.extraXuids.push_back(0x0009000000000100ull + i);
    CHECK(!AvatarItemPatch(&pkg[0], (u32)pkg.size(), opt, &error));
    opt.extraXuids.clear();

    // Non-avatar content and truncated files are refused.
    pkg = original;
    WriteBE32(&pkg[0] + kStfsOffsetContentType, kContentTypeProfile);
    CHECK(!AvatarItemPatch(&pkg[0], (u32)pkg.size(), opt, &error));
    pkg = original;
    CHECK(!AvatarItemPatch(&pkg[0], 0x2000, opt, &error));

    u32 t;
    u64 x;
    CHECK(ParseHex32("aaaaaaaa", &t) && t == 0xAAAAAAAA);
    CHECK(!ParseHex32("AAAAAAA", &t));
    CHECK(!ParseHex32("AAAAAAAG", &t));
    CHECK(ParseHex64("0009000087654321", &x) && x == 0x0009000087654321ull);
}

static void TestAccount()
{
    u8 data[kAccountDataSize];
    memset(data, 0, sizeof data);
    WriteBE32(data + 0x00, 0x00000001);
    WriteUtf16Be(data + 0x08, "OfflineTest");
    static const u8 kConfounder[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    u8 file[kAccountFileSize];
    EncryptAccount(data, kConfounder, kAccountKeyRetail, file);
    ProfileAccount acc;
    std::string error;
    CHECK(DecryptAccount(file, sizeof file, &acc, &error));
    CHECK(acc.keyType == kAccountKeyRetail);
    CHECK(acc.gamertag == "OfflineTest");
    CHECK(acc.reservedFlags == 1);
    CHECK(acc.onlineXuid == 0);

    // Setting the XUID changes only data 0x28..0x30 and keeps the confounder.
    CHECK(AccountSetOnlineXuid(file, sizeof file, 0x0009000012345678ull, false, &error));
    CHECK(DecryptAccount(file, sizeof file, &acc, &error));
    CHECK(acc.onlineXuid == 0x0009000012345678ull);
    CHECK(acc.reservedFlags == 1);
    CHECK(acc.gamertag == "OfflineTest");
    for (u32 i = 0; i < kAccountDataSize; i++)
        if ((i < 0x28 || i >= 0x30) && acc.data[i] != data[i])
        {
            CHECK(!"unexpected account byte changed");
            break;
        }
    u8 expected[kAccountFileSize];
    WriteBE64(data + 0x28, 0x0009000012345678ull);
    EncryptAccount(data, kConfounder, kAccountKeyRetail, expected);
    CHECK(memcmp(file, expected, sizeof file) == 0);

    // markLiveEnabled sets the Live bit and the generic Live fields, nothing else.
    CHECK(AccountSetOnlineXuid(file, sizeof file, 0x0009000012345678ull, true, &error));
    CHECK(DecryptAccount(file, sizeof file, &acc, &error));
    CHECK(acc.reservedFlags == (kAccountLiveEnabled | 1));
    CHECK(acc.onlineXuid == 0x0009000012345678ull);
    CHECK(memcmp(acc.data + 0x34, "PROD", 4) == 0);
    CHECK(memcmp(acc.data + 0x3C, "xbox.com\0", 9) == 0);
    CHECK(memcmp(acc.data + 0x50, "PASSPORT.NET\0", 13) == 0);
    CHECK(ReadBE32(acc.data + 0x30) == 0);
    CHECK(acc.data[0x68] == 0x81);
    for (u32 i = 0x69; i < kAccountDataSize; i++)
        if (acc.data[i] != 0)
        {
            CHECK(!"account-specific byte written");
            break;
        }

    // The result matches 360mpGui's Account template byte for byte (for this
    // gamertag and XUID; script.au3 of 360mpGui v1.5.0.0, lines 15750-15766).
    {
        u8 expectedData[kAccountDataSize];
        memset(expectedData, 0, sizeof expectedData);
        WriteBE32(expectedData + 0x00, 0x20000001);
        WriteUtf16Be(expectedData + 0x08, "OfflineTest");
        WriteBE64(expectedData + 0x28, 0x0009000012345678ull);
        memcpy(expectedData + 0x34, "PROD", 4);
        memcpy(expectedData + 0x3C, "xbox.com", 8);
        memcpy(expectedData + 0x50, "PASSPORT.NET", 12);
        expectedData[0x68] = 0x81;
        CHECK(memcmp(acc.data, expectedData, kAccountDataSize) == 0);
    }

    // Fields that are already set are kept.
    memcpy(data + 0x34, "TEST", 4);
    EncryptAccount(data, kConfounder, kAccountKeyRetail, file);
    CHECK(AccountSetOnlineXuid(file, sizeof file, 0x0009000012345678ull, true, &error));
    CHECK(DecryptAccount(file, sizeof file, &acc, &error));
    CHECK(memcmp(acc.data + 0x34, "TEST", 4) == 0);
    memset(data + 0x34, 0, 4);

    // Devkit-keyed files round-trip too; corrupted ones are refused.
    EncryptAccount(data, kConfounder, kAccountKeyDevkit, file);
    CHECK(DecryptAccount(file, sizeof file, &acc, &error) && acc.keyType == kAccountKeyDevkit);
    file[0x50] ^= 1;
    CHECK(!DecryptAccount(file, sizeof file, &acc, &error));
    CHECK(!AccountSetOnlineXuid(file, sizeof file, 1, false, &error));
}

// A throwaway 1024-bit test key (exponent 65537), made only for this test, and
// its PKCS#1 v1.5 SHA-1 signature over the bytes (i * 7 + 3) & 0xFF for i in
// 0..0x117.
static const u8 kTestModulus[128] =
{
    0x9D, 0xC9, 0xFF, 0x62, 0x2D, 0xE5, 0x30, 0x1E, 0x6A, 0xB6, 0x69, 0xDB, 0xD9, 0x00, 0x22, 0xCF,
    0x32, 0x62, 0x50, 0x3B, 0x8A, 0x84, 0x38, 0x2E, 0xB6, 0xA6, 0xC1, 0x0C, 0x6A, 0xB4, 0x49, 0xE4,
    0x29, 0x64, 0x49, 0xC3, 0x86, 0x97, 0x53, 0x21, 0x73, 0xCB, 0x1C, 0x63, 0x94, 0xF2, 0x63, 0x93,
    0xE5, 0x29, 0xBA, 0xF4, 0x95, 0x1D, 0x97, 0xE1, 0x8C, 0x89, 0x74, 0x06, 0xC6, 0xAC, 0x85, 0xA4,
    0x54, 0x28, 0x03, 0x30, 0xFA, 0xF1, 0x6E, 0x0E, 0xB9, 0x51, 0x35, 0x73, 0x3E, 0x3E, 0x41, 0xB0,
    0x82, 0xA6, 0xB3, 0x19, 0x48, 0x19, 0x67, 0x13, 0x38, 0xDE, 0x18, 0xE8, 0x5A, 0xF2, 0xBE, 0x42,
    0x02, 0xD2, 0x4C, 0x3A, 0x88, 0xCF, 0x77, 0x94, 0xB9, 0x3F, 0xC7, 0xBC, 0xB5, 0x4E, 0x4B, 0x63,
    0xAF, 0x9F, 0x54, 0x98, 0x10, 0x06, 0xEB, 0x8C, 0xBF, 0xDA, 0xA3, 0x0B, 0xAE, 0x8A, 0x1B, 0x2B
};
static const u8 kTestSignature[128] =
{
    0x74, 0xA4, 0xC3, 0x03, 0x87, 0x95, 0x32, 0x1C, 0xFE, 0x37, 0x0E, 0x01, 0x6A, 0x37, 0x90, 0xD0,
    0x3C, 0x40, 0x28, 0x1D, 0xBD, 0x0D, 0x30, 0x27, 0x5C, 0xB1, 0x02, 0x86, 0x52, 0x92, 0xF2, 0x28,
    0x5B, 0xA5, 0xD1, 0x20, 0x54, 0xCA, 0xFB, 0x01, 0xBA, 0xAE, 0x4C, 0xE7, 0x2C, 0xDA, 0x9F, 0x43,
    0x9E, 0x5C, 0x89, 0x36, 0xB1, 0xFF, 0x2E, 0xFD, 0x80, 0x19, 0x94, 0x5B, 0x0F, 0x04, 0x1E, 0x8A,
    0x83, 0x56, 0x90, 0x4A, 0x52, 0xFD, 0xA7, 0xEB, 0x92, 0xEC, 0x34, 0xAC, 0xBB, 0xF6, 0x2A, 0x81,
    0x0B, 0xD9, 0x06, 0x0D, 0xAF, 0x90, 0x77, 0x66, 0x49, 0xAC, 0xF6, 0x66, 0x97, 0x79, 0x9A, 0x90,
    0x54, 0x05, 0xE0, 0x06, 0xC8, 0xC5, 0xFD, 0xE8, 0x7B, 0x55, 0xB3, 0x40, 0xBD, 0x00, 0xB8, 0xCA,
    0x04, 0xF7, 0x04, 0x81, 0x97, 0x51, 0x2A, 0x96, 0x92, 0xF1, 0x2C, 0x9E, 0x20, 0x79, 0x9B, 0xBA
};

static void TestRsaAndConSignature()
{
    u8 region[kStfsSignedDataSize];
    for (u32 i = 0; i < sizeof region; i++)
        region[i] = (u8)(i * 7 + 3);
    u8 hash[20];
    Sha1(region, sizeof region, hash);

    CHECK(RsaVerifySha1(kTestModulus, 128, 65537, kTestSignature, hash));
    u8 bad[128];
    memcpy(bad, kTestSignature, sizeof bad);
    bad[100] ^= 1;
    CHECK(!RsaVerifySha1(kTestModulus, 128, 65537, bad, hash));
    CHECK(!RsaVerifySha1(kTestModulus, 128, 3, kTestSignature, hash));
    hash[0] ^= 1;
    CHECK(!RsaVerifySha1(kTestModulus, 128, 65537, kTestSignature, hash));
    CHECK(!RsaVerifySha1(kTestModulus, 128, 65537, kTestModulus, hash));   // not below the modulus

    // The same key and signature laid out as a CON header stores them.
    std::vector<u8> con(0x400, 0);
    memcpy(&con[0], "CON ", 4);
    memcpy(&con[kStfsOffsetSignedData], region, sizeof region);
    WriteBE32(&con[kStfsOffsetConsoleExponent], 65537);
    for (u32 block = 0; block < 16; block++)
        memcpy(&con[kStfsOffsetConsoleModulus + block * 8], kTestModulus + 120 - block * 8, 8);
    for (u32 i = 0; i < 128; i++)
        con[kStfsOffsetConsoleSigBytes + i] = kTestSignature[127 - i];
    CHECK(StfsVerifyConSignature(&con[0], (u32)con.size()));

    // ProfileApplyConsoleSignature writes the 0x228 bytes at 0x004 when they
    // verify, and leaves the header as it was when they don't.
    std::vector<u8> blob(con.begin() + kStfsOffsetConsoleSignature,
                         con.begin() + kStfsOffsetConsoleSignature + kStfsConsoleSignatureSize);
    std::vector<u8> unsigned_(con);
    memset(&unsigned_[kStfsOffsetConsoleSigBytes], 0, 128);
    std::string error;
    std::vector<u8> target(unsigned_);
    CHECK(!StfsVerifyConSignature(&target[0], (u32)target.size()));
    CHECK(ProfileApplyConsoleSignature(&target[0], (u32)target.size(), &blob[0], &error));
    CHECK(target == con);

    blob[kStfsOffsetConsoleSigBytes - kStfsOffsetConsoleSignature] ^= 1;
    target = unsigned_;
    CHECK(!ProfileApplyConsoleSignature(&target[0], (u32)target.size(), &blob[0], &error));
    CHECK(target == unsigned_);
}

static void TestItemHelpers()
{
    std::vector<u8> pkg = MakeSyntheticPackage();
    static const u8 kAssetId[16] = { 0x00, 0x00, 0x00, 0x08, 0xA2, 0x12, 0xA2, 0x31,
                                     0xC0, 0x5F, 0x8A, 0x70, 0x4D, 0x53, 0x07, 0xE6 };
    memcpy(&pkg[kAvatarOffsetAssetId], kAssetId, 16);
    WriteBE64(&pkg[kAvatarOffsetLicensee1], 0x0009000000000000ull);
    CHECK(AvatarItemPatchedFileName(&pkg[0], 0xAAAAAAAA) == "00000008A212A231C05F8A70AAAAAAAA");
    CHECK(AvatarItemFileName(&pkg[0]) == "00000008A212A231C05F8A704D5307E6");   // unchanged
    CHECK(AvatarItemIsLicensedTo(&pkg[0], 0x0009000000000000ull));
    CHECK(!AvatarItemIsLicensedTo(&pkg[0], kSharedOnlineXuid));

    // The synthetic header has no thumbnail; one within the header is found.
    const u8* png = NULL;
    u32 pngBytes = 0;
    CHECK(!StfsThumbnail(&pkg[0], (u32)pkg.size(), &png, &pngBytes));
    WriteBE32(&pkg[kStfsOffsetThumbnailSize], 300);
    CHECK(StfsThumbnail(&pkg[0], (u32)pkg.size(), &png, &pngBytes) && png == &pkg[kStfsOffsetThumbnail] &&
          pngBytes == 300);
    WriteBE32(&pkg[kStfsOffsetThumbnailSize], 0x3D01);   // too big for metadata version 2
    CHECK(!StfsThumbnail(&pkg[0], (u32)pkg.size(), &png, &pngBytes));
}

// Synthetic STFS packages, laid out from the format's description rather than
// with StfsPackage's offset formulas: after the hashed header come slots, each
// a hash table (two copies in "male" packages) or a data block, in this order:
//   level-0 table 0, data blocks 0..169, [level-1 table], level-0 table 1,
//   data blocks 170..339, level-0 table 2, ...
// The level-1 table, there from 171 blocks on, follows the first 170 data
// blocks. Level 2 (0x70E4 blocks and more, over 115 MB) isn't built.
// A hash entry: SHA-1 of the block (20), status (1), next block (BE24).
enum
{
    kSynthBlock      = 0x1000,
    kSynthPerTable   = 0xAA,
    kSynthEntry      = 0x18,
    kSynthHeaderSize = 0x971A,   // the hashed header ends at 0xA000
    kSynthBase       = 0xA000,
    kSynthVolume     = 0x379,
    kSynthEndOfChain = 0xFFFFFF,
};

struct SynthFile
{
    std::string     name;
    std::vector<u8> data;
    bool            fragmented;   // blocks allocated back to front: only the hash chain finds them
    bool            directory;
    u16             parent;       // index of the parent directory's entry, 0xFFFF for the root
};

static SynthFile MakeSynthFile(const char* name, u32 size, u8 seed, bool fragmented = false)
{
    SynthFile f;
    f.name = name;
    f.data.resize(size);
    for (u32 i = 0; i < size; i++)
        f.data[i] = (u8)(i * 7 + seed + (i >> 12));
    f.fragmented = fragmented;
    f.directory = false;
    f.parent = 0xFFFF;
    return f;
}

struct SynthOptions
{
    bool male;          // two copies of each hash table
    bool secondCopy;    // male: the volume descriptor picks the top table's second copy
    u32  contentType;
    u32  freeBlocks;    // allocated but unused blocks after the files
};

struct SynthPackage
{
    std::vector<u8>  bytes;
    u32              topLevel;
    u32              fileTableBlocks;
    std::vector<u32> dataOffset;       // per block
    std::vector<u32> table0;           // per level-0 table: the active copy's offset
    std::vector<u32> table0Inactive;   // male only
    u32              table1;           // the active level-1 copy, at top level 1
    u32              table1Inactive;   // male at top level 1
    std::vector<std::vector<u32> > chains;   // per file, its blocks in order
};

static void WriteLE16(u8* p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void WriteLE24(u8* p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); }
static void WriteBE24(u8* p, u32 v) { p[0] = (u8)(v >> 16); p[1] = (u8)(v >> 8); p[2] = (u8)v; }

static SynthPackage MakeStfsPackage(const std::vector<SynthFile>& files, const SynthOptions& o)
{
    SynthPackage pkg;
    const u32 entriesPerBlock = kSynthBlock / 0x40;
    pkg.fileTableBlocks = ((u32)files.size() + entriesPerBlock - 1) / entriesPerBlock;
    if (pkg.fileTableBlocks == 0)
        pkg.fileTableBlocks = 1;

    // Block numbers: the file table first, then each file's blocks.
    std::vector<u32> tableChain;
    for (u32 b = 0; b < pkg.fileTableBlocks; b++)
        tableChain.push_back(b);
    u32 next = pkg.fileTableBlocks;
    pkg.chains.resize(files.size());
    for (size_t i = 0; i < files.size(); i++)
    {
        u32 n = ((u32)files[i].data.size() + kSynthBlock - 1) / kSynthBlock;
        for (u32 k = 0; k < n; k++)
            pkg.chains[i].push_back(files[i].fragmented ? next + n - 1 - k : next + k);
        next += n;
    }
    u32 total = next + o.freeBlocks;
    pkg.topLevel = total <= kSynthPerTable ? 0 : 1;

    // Slots.
    u32 copies = o.male ? 2 : 1;
    u32 groups = (total + kSynthPerTable - 1) / kSynthPerTable;
    std::vector<u32> table0Slot(groups);
    std::vector<u32> dataSlot(total);
    u32 table1Slot = 0;
    u32 slot = 0;
    for (u32 g = 0; g < groups; g++)
    {
        table0Slot[g] = slot;
        slot += copies;
        for (u32 b = g * kSynthPerTable; b < total && b < (g + 1) * kSynthPerTable; b++)
            dataSlot[b] = slot++;
        if (g == 0 && pkg.topLevel == 1)
        {
            table1Slot = slot;
            slot += copies;
        }
    }
    pkg.bytes.assign(kSynthBase + slot * kSynthBlock, 0);
    u8* p = &pkg.bytes[0];

    // The active copies: the volume descriptor picks the top table's, a level-1
    // entry its level-0 table's; at level 1 they alternate, so both kinds occur.
    bool table1Second = o.male && o.secondCopy;
    std::vector<bool> table0Second(groups);
    for (u32 g = 0; g < groups; g++)
        table0Second[g] = o.male && (pkg.topLevel == 0 ? o.secondCopy : g % 2 == 1);

    for (u32 b = 0; b < total; b++)
        pkg.dataOffset.push_back(kSynthBase + dataSlot[b] * kSynthBlock);
    for (u32 g = 0; g < groups; g++)
    {
        u32 first = kSynthBase + table0Slot[g] * kSynthBlock;
        pkg.table0.push_back(first + (table0Second[g] ? kSynthBlock : 0));
        pkg.table0Inactive.push_back(o.male ? first + (table0Second[g] ? 0 : kSynthBlock) : 0);
    }
    u32 first1 = kSynthBase + table1Slot * kSynthBlock;
    pkg.table1 = pkg.topLevel ? first1 + (table1Second ? kSynthBlock : 0) : 0;
    pkg.table1Inactive = pkg.topLevel && o.male ? first1 + (table1Second ? 0 : kSynthBlock) : 0;

    // Data: a pattern in every block (unused ones and the bytes past a file's
    // end too), then the file table and the files.
    for (u32 b = 0; b < total; b++)
        for (u32 i = 0; i < kSynthBlock; i++)
            p[pkg.dataOffset[b] + i] = (u8)(b * 13 + i * 3 + 1);
    for (u32 t = 0; t < pkg.fileTableBlocks; t++)
        memset(p + pkg.dataOffset[t], 0, kSynthBlock);
    for (size_t i = 0; i < files.size(); i++)
    {
        const SynthFile& f = files[i];
        u32 blocks = (u32)pkg.chains[i].size();
        u8* e = p + pkg.dataOffset[tableChain[i / entriesPerBlock]] + (i % entriesPerBlock) * 0x40;
        memcpy(e, f.name.c_str(), f.name.size());
        e[0x28] = (u8)(f.name.size() | (f.directory ? 0x80 : 0) | (!f.fragmented && blocks ? 0x40 : 0));
        WriteLE24(e + 0x29, blocks);
        WriteLE24(e + 0x2C, blocks);
        WriteLE24(e + 0x2F, blocks ? pkg.chains[i][0] : 0);
        e[0x32] = (u8)(f.parent >> 8);
        e[0x33] = (u8)f.parent;
        WriteBE32(e + 0x34, (u32)f.data.size());
        for (u32 k = 0; k < blocks; k++)
        {
            u32 n = (u32)f.data.size() - k * kSynthBlock;
            memcpy(p + pkg.dataOffset[pkg.chains[i][k]], &f.data[k * kSynthBlock],
                   n < (u32)kSynthBlock ? n : (u32)kSynthBlock);
        }
    }

    // Level-0 entries: used blocks point to their successor (end of chain for
    // a chain's last), unused ones have status 0. Inactive copies hold junk.
    std::vector<u32> nextBlock(total, kSynthEndOfChain);
    std::vector<bool> used(total, false);
    std::vector<std::vector<u32> > allChains(pkg.chains);
    allChains.push_back(tableChain);
    for (size_t c = 0; c < allChains.size(); c++)
        for (size_t k = 0; k < allChains[c].size(); k++)
        {
            used[allChains[c][k]] = true;
            if (k + 1 < allChains[c].size())
                nextBlock[allChains[c][k]] = allChains[c][k + 1];
        }
    for (u32 b = 0; b < total; b++)
    {
        u8* e = p + pkg.table0[b / kSynthPerTable] + (b % kSynthPerTable) * kSynthEntry;
        Sha1(p + pkg.dataOffset[b], kSynthBlock, e);
        e[20] = used[b] ? 0x80 : 0x00;
        WriteBE24(e + 21, nextBlock[b]);
    }
    if (o.male)
    {
        for (u32 g = 0; g < groups; g++)
            memset(p + pkg.table0Inactive[g], 0xEE, kSynthBlock);
        if (pkg.topLevel)
            memset(p + pkg.table1Inactive, 0xEE, kSynthBlock);
    }
    if (pkg.topLevel == 1)
        for (u32 g = 0; g < groups; g++)
        {
            u8* e = p + pkg.table1 + g * kSynthEntry;
            Sha1(p + pkg.table0[g], kSynthBlock, e);
            e[20] = table0Second[g] ? 0x40 : 0x00;
        }

    memcpy(p, "CON ", 4);
    WriteBE32(p + kStfsOffsetHeaderSize, kSynthHeaderSize);
    WriteBE32(p + kStfsOffsetContentType, o.contentType);
    WriteBE32(p + kStfsOffsetMetaVersion, 2);
    WriteBE64(p + kStfsOffsetProfileId, 0xE0000123456789ABull);
    WriteUtf16Be(p + kStfsOffsetDisplayName, "Synthetic");
    u8* vol = p + kSynthVolume;
    vol[0] = 0x24;
    vol[2] = (u8)(!o.male ? 1 : (o.secondCopy ? 2 : 0));   // bit 0: female, bit 1: top table's second copy
    WriteLE16(vol + 3, pkg.fileTableBlocks);
    WriteLE24(vol + 5, tableChain[0]);
    Sha1(p + (pkg.topLevel ? pkg.table1 : pkg.table0[0]), kSynthBlock, vol + 8);
    WriteBE32(vol + 0x1C, total);

    u8 hash[20];
    StfsComputeHeaderHash(p, (u32)pkg.bytes.size(), kSynthHeaderSize, hash);
    memcpy(p + kStfsOffsetHeaderHash, hash, 20);
    return pkg;
}

struct ByteRange
{
    u32 start;
    u32 size;
};

// True when `after` differs from `before` only inside `ranges`.
static bool ChangedOnlyIn(const std::vector<u8>& before, const std::vector<u8>& after,
                          const std::vector<ByteRange>& ranges)
{
    if (before.size() != after.size())
        return false;
    std::vector<bool> allowed(before.size(), false);
    for (size_t r = 0; r < ranges.size(); r++)
        for (u32 i = 0; i < ranges[r].size; i++)
            allowed[ranges[r].start + i] = true;
    for (size_t i = 0; i < before.size(); i++)
        if (before[i] != after[i] && !allowed[i])
            return false;
    return true;
}

// What rewriting `size` bytes of file `index` may change: those bytes, the
// active level-0 entries' hashes of its blocks, the level-1 entries' hashes
// above them and the top hash.
static std::vector<ByteRange> RewriteRanges(const SynthPackage& pkg, size_t index, u32 size)
{
    std::vector<ByteRange> ranges;
    const std::vector<u32>& chain = pkg.chains[index];
    for (size_t k = 0; k < chain.size(); k++)
    {
        u32 b = chain[k];
        u32 n = size - (u32)k * kSynthBlock;
        ByteRange data = { pkg.dataOffset[b], n < (u32)kSynthBlock ? n : (u32)kSynthBlock };
        ByteRange entry0 = { pkg.table0[b / kSynthPerTable] + (b % kSynthPerTable) * kSynthEntry, 20 };
        ranges.push_back(data);
        ranges.push_back(entry0);
        if (pkg.topLevel)
        {
            ByteRange entry1 = { pkg.table1 + (b / kSynthPerTable) * kSynthEntry, 20 };
            ranges.push_back(entry1);
        }
    }
    ByteRange top = { kSynthVolume + 8, 20 };
    ranges.push_back(top);
    return ranges;
}

struct SynthConfig
{
    const char* label;
    bool        male;
    bool        secondCopy;
    u32         bigBlocks;   // 10: top level 0; 300: top level 1, three level-0 tables
};

static const SynthConfig kSynthConfigs[] =
{
    { "female, level 0",              false, false, 10 },
    { "male, first copies, level 0",  true,  false, 10 },
    { "male, second copies, level 0", true,  true,  10 },
    { "female, level 1",              false, false, 300 },
    { "male, first copies, level 1",  true,  false, 300 },
    { "male, second copies, level 1", true,  true,  300 },
};
static const size_t kSynthConfigCount = sizeof kSynthConfigs / sizeof kSynthConfigs[0];

static SynthOptions SynthOpts(const SynthConfig& config, u32 contentType, u32 freeBlocks)
{
    SynthOptions o;
    o.male = config.male;
    o.secondCopy = config.secondCopy;
    o.contentType = contentType;
    o.freeBlocks = freeBlocks;
    return o;
}

// A file table of two blocks (70 entries) with files in consecutive blocks and
// in chained ones, an empty file, and a directory with a file in it; the last
// file, `bigBlocks` long, is chained too.
enum { kSynthSmall = 0, kSynthChained = 1, kSynthExact = 2, kSynthEmpty = 3, kSynthFolder = 4, kSynthInner = 5 };

static std::vector<SynthFile> SynthFiles(u32 bigBlocks)
{
    std::vector<SynthFile> files;
    files.push_back(MakeSynthFile("small.bin", 100, 1));
    files.push_back(MakeSynthFile("chained.bin", 3 * kSynthBlock + 5, 2, true));
    files.push_back(MakeSynthFile("exact.bin", 2 * kSynthBlock, 3));
    files.push_back(MakeSynthFile("empty.bin", 0, 4));
    SynthFile folder = MakeSynthFile("folder", 0, 0);
    folder.directory = true;
    files.push_back(folder);
    SynthFile inner = MakeSynthFile("inner.bin", 50, 5);
    inner.parent = kSynthFolder;
    files.push_back(inner);
    for (int i = 0; files.size() < 69; i++)
    {
        char name[32];
        sprintf(name, "pad%02d.gpd", i);
        files.push_back(MakeSynthFile(name, 10 + i, (u8)(6 + i)));
    }
    files.push_back(MakeSynthFile("big.gpd", bigBlocks * kSynthBlock - 7, 99, true));
    return files;
}

static void CountMismatches(const std::vector<u8>& bytes, u32* blocks, u32* tables)
{
    StfsPackage stfs;
    u32 checked;
    *blocks = *tables = 0xFFFF;
    if (!stfs.Open(&bytes[0], (u32)bytes.size(), NULL))
        return;
    *blocks = stfs.VerifyAllBlocks(&checked);
    *tables = stfs.VerifyHashTables(&checked);
}

static void TestStfsPackageRead()
{
    for (size_t c = 0; c < kSynthConfigCount; c++)
    {
        const SynthConfig& config = kSynthConfigs[c];
        g_context = config.label;
        std::vector<SynthFile> files = SynthFiles(config.bigBlocks);
        SynthPackage pkg = MakeStfsPackage(files, SynthOpts(config, kContentTypeMarketplace, 5));
        CHECK(pkg.topLevel == (config.bigBlocks > kSynthPerTable ? 1u : 0u));

        StfsPackage stfs;
        std::string error;
        CHECK(stfs.Open(&pkg.bytes[0], (u32)pkg.bytes.size(), &error));
        CHECK(stfs.Files().size() == files.size());
        if (stfs.Files().size() != files.size())
            continue;
        u32 usedBlocks = pkg.fileTableBlocks;
        for (size_t i = 0; i < files.size(); i++)
        {
            const StfsFileEntry& entry = stfs.Files()[i];
            CHECK(entry.name == files[i].name);
            CHECK(entry.isDirectory == files[i].directory);
            CHECK(entry.parentIndex == files[i].parent);
            if (files[i].directory)
                continue;
            CHECK(entry.consecutive == (!files[i].fragmented && !files[i].data.empty()));
            std::vector<u8> out;
            CHECK(stfs.ReadFile(entry, &out, &error) && out == files[i].data);
            usedBlocks += (u32)pkg.chains[i].size();
        }

        // Root-level files only, ignoring case.
        CHECK(stfs.FindFile("SMALL.BIN") == &stfs.Files()[kSynthSmall]);
        CHECK(stfs.FindFile("big.gpd") == &stfs.Files().back());
        CHECK(stfs.FindFile("inner.bin") == NULL);
        CHECK(stfs.FindFile("folder") == NULL);
        CHECK(stfs.FindFile("small.bi") == NULL);

        u32 checked = 0;
        CHECK(stfs.VerifyAllBlocks(&checked) == 0);
        CHECK(checked == usedBlocks);
        CHECK(stfs.VerifyHashTables(&checked) == 0);
        CHECK(checked == pkg.table0.size() + 1);
    }
    g_context = NULL;
}

static void TestStfsPackageReplace()
{
    for (size_t c = 0; c < kSynthConfigCount; c++)
    {
        const SynthConfig& config = kSynthConfigs[c];
        g_context = config.label;
        std::vector<SynthFile> files = SynthFiles(config.bigBlocks);
        SynthPackage pkg = MakeStfsPackage(files, SynthOpts(config, kContentTypeMarketplace, 5));
        StfsPackage stfs;
        std::string error;
        CHECK(stfs.Open(&pkg.bytes[0], (u32)pkg.bytes.size(), &error));

        // A chained file of four blocks, and one that spans level-0 tables at level 1.
        const size_t targets[] = { kSynthChained, files.size() - 1 };
        for (size_t t = 0; t < 2; t++)
        {
            size_t index = targets[t];
            u32 size = (u32)files[index].data.size();
            std::vector<u8> content(size);
            for (u32 i = 0; i < size; i++)
                content[i] = (u8)(i * 11 + 5);
            std::vector<u8> before = pkg.bytes;
            CHECK(stfs.ReplaceFileData(&pkg.bytes[0], stfs.Files()[index], &content[0], size, &error));
            CHECK(ChangedOnlyIn(before, pkg.bytes, RewriteRanges(pkg, index, size)));

            std::vector<u8> out;
            CHECK(stfs.ReadFile(stfs.Files()[index], &out, &error) && out == content);
            u32 checked;
            CHECK(stfs.VerifyAllBlocks(&checked) == 0);
            CHECK(stfs.VerifyHashTables(&checked) == 0);
        }

        // The top hash is inside the hashed header, whose hash is the caller's job.
        StfsHeader h;
        CHECK(StfsParseHeader(&pkg.bytes[0], (u32)pkg.bytes.size(), &h, &error));
        CHECK(h.headerHashChecked && !h.headerHashValid);

        // Another size, or a directory, is refused without changing anything.
        std::vector<u8> before = pkg.bytes;
        std::vector<u8> content(files[kSynthChained].data.size() + 1, 0);
        CHECK(!stfs.ReplaceFileData(&pkg.bytes[0], stfs.Files()[kSynthChained], &content[0],
                                    (u32)content.size() - 2, &error));
        CHECK(!stfs.ReplaceFileData(&pkg.bytes[0], stfs.Files()[kSynthChained], &content[0],
                                    (u32)content.size(), &error));
        CHECK(!stfs.ReplaceFileData(&pkg.bytes[0], stfs.Files()[kSynthFolder], &content[0], 0, &error));
        CHECK(pkg.bytes == before);
    }
    g_context = NULL;
}

static void TestStfsPackageDamage()
{
    for (size_t c = 0; c < kSynthConfigCount; c++)
    {
        const SynthConfig& config = kSynthConfigs[c];
        g_context = config.label;
        std::vector<SynthFile> files = SynthFiles(config.bigBlocks);
        const SynthPackage pkg = MakeStfsPackage(files, SynthOpts(config, kContentTypeMarketplace, 5));
        const u32 lastBlock = (u32)pkg.dataOffset.size() - 1;   // an unused one
        u32 blocks, tables;
        std::vector<u8> bytes;

        // A file's byte: that block's hash.
        bytes = pkg.bytes;
        bytes[pkg.dataOffset[pkg.chains[kSynthChained][1]] + 9] ^= 1;
        CountMismatches(bytes, &blocks, &tables);
        CHECK(blocks == 1 && tables == 0);

        // A byte past a file's end: the hash covers the whole block.
        bytes = pkg.bytes;
        bytes[pkg.dataOffset[pkg.chains[kSynthSmall][0]] + 200] ^= 1;
        CountMismatches(bytes, &blocks, &tables);
        CHECK(blocks == 1 && tables == 0);

        // An unused block isn't checked.
        bytes = pkg.bytes;
        bytes[pkg.dataOffset[lastBlock] + 9] ^= 1;
        CountMismatches(bytes, &blocks, &tables);
        CHECK(blocks == 0 && tables == 0);

        // A block's hash in the active level-0 table: the block, and the table's own hash.
        u32 b = pkg.chains[kSynthChained][0];
        bytes = pkg.bytes;
        bytes[pkg.table0[b / kSynthPerTable] + (b % kSynthPerTable) * kSynthEntry + 3] ^= 1;
        CountMismatches(bytes, &blocks, &tables);
        CHECK(blocks == 1 && tables == 1);

        // The top hash.
        bytes = pkg.bytes;
        bytes[kSynthVolume + 8] ^= 1;
        CountMismatches(bytes, &blocks, &tables);
        CHECK(blocks == 0 && tables == 1);

        if (pkg.topLevel == 1)
        {
            // A level-0 table's hash in the level-1 table: that entry, and the top hash.
            bytes = pkg.bytes;
            bytes[pkg.table1 + 2 * kSynthEntry + 3] ^= 1;
            CountMismatches(bytes, &blocks, &tables);
            CHECK(blocks == 0 && tables == 2);
        }

        if (config.male)
        {
            // Inactive copies don't count.
            bytes = pkg.bytes;
            for (size_t g = 0; g < pkg.table0Inactive.size(); g++)
                bytes[pkg.table0Inactive[g] + 3] ^= 1;
            if (pkg.topLevel)
                bytes[pkg.table1Inactive + 3] ^= 1;
            CountMismatches(bytes, &blocks, &tables);
            CHECK(blocks == 0 && tables == 0);
        }

        // Cut off in the middle of the last file: it can't be read, and counts as one bad chain.
        bytes.assign(pkg.bytes.begin(), pkg.bytes.begin() + pkg.dataOffset[pkg.chains.back()[0]]);
        StfsPackage cut;
        std::string error;
        std::vector<u8> out;
        CHECK(cut.Open(&bytes[0], (u32)bytes.size(), &error));
        CHECK(!cut.ReadFile(cut.Files().back(), &out, &error));
        u32 checked;
        CHECK(cut.VerifyAllBlocks(&checked) == 1);

        // A file table outside the package.
        bytes = pkg.bytes;
        WriteLE24(&bytes[kSynthVolume + 5], lastBlock + 10);
        StfsPackage broken;
        CHECK(!broken.Open(&bytes[0], (u32)bytes.size(), &error));
    }
    g_context = NULL;
}

// A profile's files: an offline profile's Account file (gamertag "Synth Gamer")
// and GPDs, two of them chained, one `bigBlocks` long.
enum { kSynthAccount = 1, kSynthGpd = 3 };

static std::vector<SynthFile> SynthProfileFiles(u32 bigBlocks)
{
    u8 data[kAccountDataSize];
    memset(data, 0, sizeof data);
    WriteBE32(data, 0x00000001);
    WriteUtf16Be(data + 0x08, "Synth Gamer");
    static const u8 kConfounder[8] = { 8, 7, 6, 5, 4, 3, 2, 1 };

    std::vector<SynthFile> files;
    files.push_back(MakeSynthFile("FFFE07D1.gpd", 5000, 1));
    files.push_back(MakeSynthFile("Account", kAccountFileSize, 0));
    EncryptAccount(data, kConfounder, kAccountKeyRetail, &files.back().data[0]);
    files.push_back(MakeSynthFile("4D5307E6.gpd", bigBlocks * kSynthBlock - 100, 2, true));
    files.push_back(MakeSynthFile("584108CE.gpd", 9000, 3, true));
    return files;
}

static void TestProfileRewrite()
{
    for (size_t c = 0; c < kSynthConfigCount; c++)
    {
        const SynthConfig& config = kSynthConfigs[c];
        g_context = config.label;
        std::vector<SynthFile> files = SynthProfileFiles(config.bigBlocks);
        SynthPackage pkg = MakeStfsPackage(files, SynthOpts(config, kContentTypeProfile, 3));
        const std::vector<u8> original = pkg.bytes;
        const u32 size = (u32)original.size();
        std::string error;

        ProfileInfo info;
        CHECK(ProfileInspect(&pkg.bytes[0], size, &info, &error));
        CHECK(info.kind == kProfileOffline);
        CHECK(info.gamertag == "Synth Gamer");
        CHECK(info.offlineXuid == 0xE0000123456789ABull);
        CHECK(info.onlineXuid == 0);
        CHECK(!info.signatureValid);

        // Activating changes the Account file, the hashes above it and the header hash, nothing else.
        CHECK(ProfileSetOnlineXuid(&pkg.bytes[0], size, kSharedOnlineXuid, true, &error));
        std::vector<ByteRange> ranges = RewriteRanges(pkg, kSynthAccount, kAccountFileSize);
        ByteRange headerHash = { kStfsOffsetHeaderHash, 20 };
        ranges.push_back(headerHash);
        CHECK(ChangedOnlyIn(original, pkg.bytes, ranges));
        CHECK(ProfileInspect(&pkg.bytes[0], size, &info, &error));
        CHECK(info.kind == kProfileActivated);
        CHECK(info.onlineXuid == kSharedOnlineXuid);
        CHECK((info.reservedFlags & kAccountLiveEnabled) != 0);
        CHECK(info.gamertag == "Synth Gamer");

        StfsHeader h;
        CHECK(StfsParseHeader(&pkg.bytes[0], size, &h, &error) && h.headerHashValid);
        StfsPackage stfs;
        CHECK(stfs.Open(&pkg.bytes[0], size, &error));
        u32 checked;
        CHECK(stfs.VerifyAllBlocks(&checked) == 0);
        CHECK(stfs.VerifyHashTables(&checked) == 0);
        for (size_t i = 0; i < files.size(); i++)
        {
            std::vector<u8> out;
            if (i != kSynthAccount)
                CHECK(stfs.ReadFile(stfs.Files()[i], &out, &error) && out == files[i].data);
        }

        // Another online XUID makes it a Live profile.
        pkg.bytes = original;
        CHECK(ProfileSetOnlineXuid(&pkg.bytes[0], size, 0x0009000012345678ull, true, &error));
        CHECK(ProfileInspect(&pkg.bytes[0], size, &info, &error));
        CHECK(info.kind == kProfileLive && info.onlineXuid == 0x0009000012345678ull);

        // Damage elsewhere, here a block of another GPD, makes the rewrite fail
        // although the Account file is fine: the check after it covers the
        // whole package (what happened to the "Kuba 100" profile).
        std::vector<u8> damaged = original;
        damaged[pkg.dataOffset[pkg.chains[kSynthGpd][1]] + 10] ^= 1;
        error.clear();
        CHECK(!ProfileSetOnlineXuid(&damaged[0], size, kSharedOnlineXuid, true, &error));
        CHECK(error == "the rewritten profile failed verification");

        // Junk in an inactive hash table copy doesn't matter.
        if (config.male)
        {
            damaged = original;
            damaged[pkg.table0Inactive[0] + 3] ^= 1;
            CHECK(ProfileSetOnlineXuid(&damaged[0], size, kSharedOnlineXuid, true, &error));
        }
    }
    g_context = NULL;
}

static void TestProfileRefused()
{
    SynthOptions o = SynthOpts(kSynthConfigs[1], kContentTypeProfile, 0);
    std::string error;
    ProfileInfo info;

    // Not a profile.
    std::vector<SynthFile> files = SynthProfileFiles(10);
    o.contentType = kContentTypeMarketplace;
    SynthPackage pkg = MakeStfsPackage(files, o);
    CHECK(!ProfileInspect(&pkg.bytes[0], (u32)pkg.bytes.size(), &info, &error));
    CHECK(error == "not a profile");
    CHECK(!ProfileSetOnlineXuid(&pkg.bytes[0], (u32)pkg.bytes.size(), kSharedOnlineXuid, true, &error));
    o.contentType = kContentTypeProfile;

    // An Account file that isn't at the root is no Account file.
    SynthFile folder = MakeSynthFile("folder", 0, 0);
    folder.directory = true;
    files.push_back(folder);
    files[kSynthAccount].parent = (u16)(files.size() - 1);
    pkg = MakeStfsPackage(files, o);
    error.clear();
    CHECK(!ProfileInspect(&pkg.bytes[0], (u32)pkg.bytes.size(), &info, &error));
    CHECK(error == "no Account file");
    CHECK(!ProfileSetOnlineXuid(&pkg.bytes[0], (u32)pkg.bytes.size(), kSharedOnlineXuid, true, &error));

    // An Account file that doesn't decrypt.
    files = SynthProfileFiles(10);
    files[kSynthAccount].data[0x40] ^= 1;
    pkg = MakeStfsPackage(files, o);
    error.clear();
    CHECK(!ProfileInspect(&pkg.bytes[0], (u32)pkg.bytes.size(), &info, &error));
    CHECK(!error.empty());
    std::vector<u8> before = pkg.bytes;
    CHECK(!ProfileSetOnlineXuid(&pkg.bytes[0], (u32)pkg.bytes.size(), kSharedOnlineXuid, true, &error));
    CHECK(pkg.bytes == before);
}

int main()
{
    TestSha1();
    TestStfsHeader();
    TestAvatarPatch();
    TestAccount();
    TestRsaAndConSignature();
    TestItemHelpers();
    TestStfsPackageRead();
    TestStfsPackageReplace();
    TestStfsPackageDamage();
    TestProfileRewrite();
    TestProfileRefused();

    if (g_failures)
    {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
