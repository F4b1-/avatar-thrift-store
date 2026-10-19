// Unit tests for the core library (PC only). Minimal harness, no dependencies.

#include "../core/account.h"
#include "../core/avatar_item.h"
#include "../core/bytes.h"
#include "../core/profile.h"
#include "../core/rsa.h"
#include "../core/sha1.h"
#include "../core/stfs.h"

#include <stdio.h>
#include <string.h>
#include <vector>

static int g_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

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

    // Body type: the low nibble of asset id byte 7 (...A231... male, ...8242... female).
    CHECK(AvatarItemBodyType(&pkg[0]) == kAvatarBodyMale);
    pkg[kAvatarOffsetAssetBody] = 0x42;
    CHECK(AvatarItemBodyType(&pkg[0]) == kAvatarBodyFemale);
    pkg[kAvatarOffsetAssetBody] = 0x13;
    CHECK(AvatarItemBodyType(&pkg[0]) == kAvatarBodyBoth);
    pkg[kAvatarOffsetAssetBody] = 0x07;
    CHECK(AvatarItemBodyType(&pkg[0]) == kAvatarBodyUnknown);
    CHECK(strcmp(AvatarBodyTypeName(kAvatarBodyFemale), "female") == 0);
    pkg[kAvatarOffsetAssetBody] = 0x31;

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

int main()
{
    TestSha1();
    TestStfsHeader();
    TestAvatarPatch();
    TestAccount();
    TestRsaAndConSignature();
    TestItemHelpers();

    if (g_failures)
    {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
