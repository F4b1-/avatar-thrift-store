// avtool - PC command-line companion for developing and testing the core
// library against real package files.
//
//   avtool info <package> [<package> ...]
//   avtool files <package>      list contents, verify block hashes
//   avtool profile [--raw] <profile>   decrypt the Account file (gamertag,
//       online XUID); --raw dumps all of its decrypted data
//   avtool extract <package> <name> <out>   copy one root-level file out
//   avtool awards <profile>     avatar award records (PEC) and title counters
//   avtool patch [--xuid <16 hex>] [--title <8 hex>] --out <dir> <package> [...]
//       --xuid sets the licensee of license entry [1]; omitted, it stays as is
//       writes <dir>\<TITLE>\00009000\<ASSETID> for each package
//   avtool set-xuid [--xuid <16 hex>] [--live] --out <file> <profile>
//       writes a copy of the profile with a new online XUID in its Account
//       file (default: the shared 00090000AAAAAAAA), rehashed; --live also
//       sets the Live-enabled reserved flag. The console signature is NOT
//       updated (resign it)
//   avtool verify-sig <package> [...]   check a CON package's console signature

#include "../../core/account.h"
#include "../../core/avatar_item.h"
#include "../../core/bytes.h"
#include "../../core/gpd.h"
#include "../../core/profile.h"
#include "../../core/stfs.h"
#include "../../core/stfs_package.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#define STRICMP(a, b) _stricmp(a, b)
#else
#include <strings.h>
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0777)
#define STRICMP(a, b) strcasecmp(a, b)
#endif
#include <stdio.h>
#include <string.h>
#include <vector>

static bool ReadFilePrefix(const char* path, u32 maxBytes, std::vector<u8>* out)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    out->resize(maxBytes);
    size_t n = fread(&(*out)[0], 1, maxBytes, f);
    fclose(f);
    out->resize(n);
    return true;
}

static int CmdInfo(const char* path)
{
    std::vector<u8> data;
    if (!ReadFilePrefix(path, kStfsMaxHeaderRead, &data))
    {
        fprintf(stderr, "%s: cannot open file\n", path);
        return 1;
    }

    StfsHeader h;
    std::string error;
    if (!StfsParseHeader(data.empty() ? NULL : &data[0], (u32)data.size(), &h, &error))
    {
        fprintf(stderr, "%s: %s\n", path, error.c_str());
        return 1;
    }

    printf("%s\n", path);
    printf("  magic            %s\n", StfsMagicName(h.magic));
    printf("  display name     %s\n", h.displayName.c_str());
    printf("  title name       %s\n", h.titleName.c_str());
    printf("  content type     %08X (%s)\n", h.contentType, StfsContentTypeName(h.contentType));
    printf("  title id         %08X\n", h.titleId);
    printf("  profile id       %016llX\n", h.profileId);
    printf("  console id       %s\n", HexString(h.consoleId, 5).c_str());
    printf("  device id        %s\n", HexString(h.deviceId, 20).c_str());
    printf("  metadata version %u\n", h.metadataVersion);
    printf("  content size     %llu\n", h.contentSize);
    printf("  header size      %08X\n", h.headerSize);
    printf("  header hash      %s (%s)\n", HexString(h.headerHash, 20).c_str(),
           !h.headerHashChecked ? "not checked, file too short"
                                : (h.headerHashValid ? "valid" : "MISMATCH"));
    if (h.contentType == kContentTypeAvatarItem)
        printf("  asset id         %s\n", AvatarItemFileName(&data[0]).c_str());
    printf("  licenses\n");
    for (int i = 0; i < kStfsLicenseCount; i++)
    {
        const StfsLicense& l = h.licenses[i];
        if (l.licenseeId == 0 && l.bits == 0 && l.flags == 0)
            continue;
        printf("    [%2d] licensee %016llX  bits %08X  flags %08X\n", i, l.licenseeId, l.bits, l.flags);
    }
    printf("\n");
    return 0;
}

static bool ReadWholeFile(const char* path, std::vector<u8>* out)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    out->resize(size > 0 ? size : 0);
    size_t n = size > 0 ? fread(&(*out)[0], 1, size, f) : 0;
    fclose(f);
    return n == out->size();
}

// Creates every directory along `path` (backslash separated).
static void MakeDirs(const std::string& path)
{
    for (size_t i = 1; i <= path.size(); i++)
        if (i == path.size() || path[i] == '\\' || path[i] == '/')
            MKDIR(path.substr(0, i).c_str());
}

static bool OpenPackage(const char* path, std::vector<u8>* data, StfsPackage* pkg)
{
    std::string error;
    if (!ReadWholeFile(path, data) || data->empty())
    {
        fprintf(stderr, "%s: cannot read\n", path);
        return false;
    }
    if (!pkg->Open(&(*data)[0], (u32)data->size(), &error) &&
        !pkg->OpenPec(&(*data)[0], (u32)data->size(), &error))
    {
        fprintf(stderr, "%s: %s\n", path, error.c_str());
        return false;
    }
    return true;
}

static int CmdFiles(const char* path)
{
    std::vector<u8> data;
    StfsPackage pkg;
    if (!OpenPackage(path, &data, &pkg))
        return 1;

    printf("%s\n", path);
    const std::vector<StfsFileEntry>& files = pkg.Files();
    for (size_t i = 0; i < files.size(); i++)
    {
        const StfsFileEntry& f = files[i];
        printf("  %-3u %-40s %s parent %04X start %6u blocks %5u size %u\n", (u32)i, f.name.c_str(),
               f.isDirectory ? "dir " : (f.consecutive ? "cons" : "    "),
               f.parentIndex, f.startBlock, f.blockCount, f.size);
    }
    u32 checked;
    u32 bad = pkg.VerifyAllBlocks(&checked);
    printf("  block hashes: %u checked, %u mismatched\n", checked, bad);
    u32 tablesChecked;
    u32 tablesBad = pkg.VerifyHashTables(&tablesChecked);
    printf("  hash tables:  %u checked, %u mismatched\n\n", tablesChecked, tablesBad);
    return bad || tablesBad ? 1 : 0;
}

// Hex and ASCII, 16 bytes a line.
static void HexDump(const u8* data, u32 size)
{
    for (u32 row = 0; row < size; row += 16)
    {
        printf("    %04X ", row);
        for (u32 i = row; i < row + 16; i++)
            printf(i < size ? " %02X" : "   ", i < size ? data[i] : 0);
        printf("  ");
        for (u32 i = row; i < row + 16 && i < size; i++)
            putchar(data[i] >= 0x20 && data[i] < 0x7F ? data[i] : '.');
        printf("\n");
    }
}

static int CmdProfile(const char* path, bool raw)
{
    std::vector<u8> data;
    StfsPackage pkg;
    if (!OpenPackage(path, &data, &pkg))
        return 1;

    const StfsFileEntry* entry = pkg.FindFile("Account");
    std::vector<u8> account;
    std::string error;
    if (!entry || !pkg.ReadFile(*entry, &account, &error))
    {
        fprintf(stderr, "%s: no readable Account file %s\n", path, error.c_str());
        return 1;
    }

    ProfileAccount acc;
    if (!DecryptAccount(&account[0], (u32)account.size(), &acc, &error))
    {
        fprintf(stderr, "%s: %s\n", path, error.c_str());
        return 1;
    }

    StfsHeader h;
    StfsParseHeader(&data[0], (u32)data.size(), &h, &error);
    printf("%s\n", path);
    printf("  gamertag        %s\n", acc.gamertag.c_str());
    printf("  offline xuid    %016llX\n", h.profileId);
    printf("  online xuid     %016llX%s\n", acc.onlineXuid, acc.onlineXuid ? "" : " (none, offline profile)");
    printf("  console id      %s\n", HexString(h.consoleId, 5).c_str());
    printf("  reserved flags  %08X\n", acc.reservedFlags);
    printf("  live flags      %08X\n", acc.liveFlags);
    printf("  key             %s\n", acc.keyType == kAccountKeyRetail ? "retail" : "devkit");
    if (raw)
    {
        printf("  decrypted account data:\n");
        HexDump(acc.data, kAccountDataSize);
        printf("\n");
    }
    else
        printf("  data[0x00..0x40] %s\n\n", HexString(acc.data, 0x40).c_str());
    return 0;
}

static bool ReadPackageFile(const StfsPackage& pkg, const char* name, std::vector<u8>* out)
{
    const StfsFileEntry* entry = pkg.FindFile(name);
    std::string error;
    return entry && pkg.ReadFile(*entry, out, &error);
}

static int CmdAwards(const char* path)
{
    std::vector<u8> data;
    StfsPackage profile;
    if (!OpenPackage(path, &data, &profile))
        return 1;
    printf("%s\n", path);

    // Title records with avatar award counters, from the dashboard GPD.
    std::vector<u8> dash;
    GpdFile dashGpd;
    std::string error;
    if (ReadPackageFile(profile, "FFFE07D1.gpd", &dash) && GpdParse(&dash[0], (u32)dash.size(), &dashGpd, &error))
    {
        printf("  dashboard title entries with avatar awards:\n");
        for (size_t i = 0; i < dashGpd.entries.size(); i++)
        {
            const GpdEntry& e = dashGpd.entries[i];
            if (e.ns != kGpdTitle || e.length < 0x28 || e.id <= kGpdSyncDataId)
                continue;
            const u8* t = dashGpd.EntryData(e);
            if (t[0x17] == 0 && t[0x16] == 0)
                continue;
            u32 pos = 0x28;
            std::string name;
            while (pos + 1 < e.length && ReadBE16(t + pos) != 0)
            {
                u16 c = ReadBE16(t + pos);
                name += c < 0x80 ? (char)c : '?';
                pos += 2;
            }
            printf("    %08X %-30s awards %u/%u  male %u/%u  female %u/%u  flags %08X\n",
                   ReadBE32(t), name.c_str(), t[0x16], t[0x17], t[0x18], t[0x19], t[0x1A], t[0x1B],
                   ReadBE32(t + 0x1C));
        }
    }

    std::vector<u8> pecData;
    StfsPackage pec;
    if (!ReadPackageFile(profile, "PEC", &pecData) || !pec.OpenPec(&pecData[0], (u32)pecData.size(), &error))
    {
        printf("  no PEC\n\n");
        return 0;
    }

    printf("  PEC award records:\n");
    const std::vector<StfsFileEntry>& files = pec.Files();
    for (size_t f = 0; f < files.size(); f++)
    {
        std::vector<u8> raw;
        GpdFile gpd;
        if (!pec.ReadFile(files[f], &raw, &error) || raw.empty() ||
            !GpdParse(&raw[0], (u32)raw.size(), &gpd, &error))
            continue;
        printf("    %s (version %08X, %u entries)\n", files[f].name.c_str(), gpd.version, (u32)gpd.entries.size());
        for (size_t i = 0; i < gpd.entries.size(); i++)
        {
            const GpdEntry& e = gpd.entries[i];
            if (e.ns != kGpdAvatarAward)
                continue;
            if (e.id <= kGpdSyncDataId)
            {
                printf("      sync entry %llu (%u bytes): %s\n", e.id, e.length,
                       HexString(gpd.EntryData(e), e.length > 32 ? 32 : e.length).c_str());
                continue;
            }
            GpdAvatarAward a;
            if (!GpdParseAvatarAward(gpd.EntryData(e), e.length, &a))
            {
                printf("      %016llX unparsed (%u bytes)\n", e.id, e.length);
                continue;
            }
            printf("      %016llX asset %s img %u flags %08X time %016llX sub %u unk %u\n"
                   "        \"%s\" / \"%s\" / \"%s\"\n",
                   e.id, HexString(a.assetId, 16).c_str(), a.imageId, a.flags, a.unlockTime,
                   a.subcategory, a.unknown, a.name.c_str(), a.unlockedDescription.c_str(),
                   a.lockedDescription.c_str());
        }
    }
    printf("\n");
    return 0;
}

static int CmdExtract(const char* path, const char* name, const char* outPath)
{
    std::vector<u8> data;
    StfsPackage pkg;
    if (!OpenPackage(path, &data, &pkg))
        return 1;

    const StfsFileEntry* entry = pkg.FindFile(name);
    std::vector<u8> content;
    std::string error;
    if (!entry || !pkg.ReadFile(*entry, &content, &error))
    {
        fprintf(stderr, "%s: cannot read %s %s\n", path, name, error.c_str());
        return 1;
    }
    FILE* f = fopen(outPath, "wb");
    if (!f)
    {
        fprintf(stderr, "%s: cannot write\n", outPath);
        return 1;
    }
    if (!content.empty())
        fwrite(&content[0], 1, content.size(), f);
    fclose(f);
    printf("%s -> %s (%u bytes)\n", name, outPath, (u32)content.size());
    return 0;
}

static int CmdPatch(int argc, char** argv)
{
    AvatarPatchOptions options;
    options.newTitleId = kAvatarDefaultTitleId;
    options.setXuid = false;
    options.xuid = 0;
    const char* outDir = NULL;
    std::vector<const char*> inputs;

    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--xuid") == 0 && i + 1 < argc)
        {
            if (!ParseHex64(argv[++i], &options.xuid))
            {
                fprintf(stderr, "invalid XUID (need 16 hex digits): %s\n", argv[i]);
                return 2;
            }
            options.setXuid = true;
        }
        else if (strcmp(argv[i], "--title") == 0 && i + 1 < argc)
        {
            if (!ParseHex32(argv[++i], &options.newTitleId))
            {
                fprintf(stderr, "invalid title id (need 8 hex digits): %s\n", argv[i]);
                return 2;
            }
        }
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
        {
            outDir = argv[++i];
        }
        else
        {
            inputs.push_back(argv[i]);
        }
    }

    if (!outDir || inputs.empty())
    {
        fprintf(stderr, "patch needs --out and at least one package\n");
        return 2;
    }

    std::string target = std::string(outDir) + "\\" + AvatarItemContentSubfolder(options.newTitleId);
    MakeDirs(target);

    int rc = 0;
    for (size_t i = 0; i < inputs.size(); i++)
    {
        std::vector<u8> data;
        std::string error;
        if (!ReadWholeFile(inputs[i], &data) || data.empty())
        {
            fprintf(stderr, "%s: cannot read\n", inputs[i]);
            rc = 1;
            continue;
        }
        if (!AvatarItemPatch(&data[0], (u32)data.size(), options, &error))
        {
            fprintf(stderr, "%s: %s\n", inputs[i], error.c_str());
            rc = 1;
            continue;
        }

        std::string outPath = target + "\\" + AvatarItemFileName(&data[0]);
        FILE* f = fopen(outPath.c_str(), "wb");
        if (!f || fwrite(&data[0], 1, data.size(), f) != data.size())
        {
            fprintf(stderr, "%s: cannot write\n", outPath.c_str());
            if (f) fclose(f);
            rc = 1;
            continue;
        }
        fclose(f);
        printf("%s -> %s\n", inputs[i], outPath.c_str());
    }
    return rc;
}

static int CmdSetXuid(int argc, char** argv)
{
    u64 xuid = kSharedOnlineXuid;
    bool live = false;
    const char* outPath = NULL;
    const char* inPath = NULL;
    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--xuid") == 0 && i + 1 < argc)
        {
            if (!ParseHex64(argv[++i], &xuid))
            {
                fprintf(stderr, "invalid XUID (need 16 hex digits): %s\n", argv[i]);
                return 2;
            }
        }
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            outPath = argv[++i];
        else if (strcmp(argv[i], "--live") == 0)
            live = true;
        else if (!inPath)
            inPath = argv[i];
        else
        {
            fprintf(stderr, "set-xuid takes one profile\n");
            return 2;
        }
    }
    if (!outPath || !inPath)
    {
        fprintf(stderr, "set-xuid needs --out and a profile\n");
        return 2;
    }
    if (STRICMP(inPath, outPath) == 0)
    {
        fprintf(stderr, "--out must differ from the input; the original profile is never overwritten\n");
        return 2;
    }
    if ((xuid >> 48) != 0x0009)
        fprintf(stderr, "warning: %016llX doesn't look like an online XUID (0009...)\n", xuid);

    std::vector<u8> data;
    ProfileInfo before;
    ProfileInfo after;
    std::string error;
    if (!ReadWholeFile(inPath, &data) || data.empty())
    {
        fprintf(stderr, "%s: cannot read\n", inPath);
        return 1;
    }
    if (!ProfileInspect(&data[0], (u32)data.size(), &before, &error) ||
        !ProfileSetOnlineXuid(&data[0], (u32)data.size(), xuid, live, &error) ||
        !ProfileInspect(&data[0], (u32)data.size(), &after, &error))
    {
        fprintf(stderr, "%s: %s, nothing written\n", inPath, error.c_str());
        return 1;
    }

    FILE* f = fopen(outPath, "wb");
    if (!f || fwrite(&data[0], 1, data.size(), f) != data.size())
    {
        fprintf(stderr, "%s: cannot write\n", outPath);
        if (f) fclose(f);
        return 1;
    }
    fclose(f);
    printf("%s (%s): online xuid %016llX -> %016llX, reserved flags %08X -> %08X\n", inPath,
           after.gamertag.c_str(), before.onlineXuid, after.onlineXuid, before.reservedFlags, after.reservedFlags);
    printf("  wrote %s: block hashes, hash tables and the header hash valid\n", outPath);
    printf("  the console signature is the old one: resign the profile before using it\n");
    return 0;
}

static int CmdVerifySig(const char* path)
{
    std::vector<u8> data;
    if (!ReadFilePrefix(path, kStfsMaxHeaderRead, &data) || data.size() < kStfsOffsetSignedData + kStfsSignedDataSize)
    {
        fprintf(stderr, "%s: cannot read\n", path);
        return 1;
    }
    if (memcmp(&data[0], "CON ", 4) != 0)
    {
        printf("%s: not a CON package, only console signatures are checked\n", path);
        return 1;
    }
    bool valid = StfsVerifyConSignature(&data[0], (u32)data.size());
    printf("%s: console signature %s, certificate of console %s\n", path, valid ? "VALID" : "INVALID",
           HexString(&data[6], 5).c_str());
    return valid ? 0 : 1;
}

static void Usage()
{
    fprintf(stderr,
            "usage: avtool info <package> [<package> ...]\n"
            "       avtool files <package> [...]\n"
            "       avtool profile [--raw] <profile> [...]\n"
            "       avtool extract <package> <file name> <output file>\n"
            "       avtool awards <profile> [...]\n"
            "       avtool patch [--xuid <16 hex>] [--title <8 hex>] --out <dir> <package> [...]\n"
            "       avtool set-xuid [--xuid <16 hex>] [--live] --out <file> <profile>\n"
            "       avtool verify-sig <package> [...]\n");
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        Usage();
        return 2;
    }

    std::string cmd = argv[1];
    if (cmd == "info")
    {
        int rc = 0;
        for (int i = 2; i < argc; i++)
            rc |= CmdInfo(argv[i]);
        return rc;
    }
    if (cmd == "patch")
        return CmdPatch(argc, argv);
    if (cmd == "set-xuid")
        return CmdSetXuid(argc, argv);
    if (cmd == "extract" && argc == 5)
        return CmdExtract(argv[2], argv[3], argv[4]);
    if (cmd == "profile")
    {
        bool raw = false;
        for (int i = 2; i < argc; i++)
            raw = raw || strcmp(argv[i], "--raw") == 0;
        int rc = 0;
        for (int i = 2; i < argc; i++)
            if (strcmp(argv[i], "--raw") != 0)
                rc |= CmdProfile(argv[i], raw);
        return rc;
    }
    if (cmd == "files" || cmd == "awards" || cmd == "verify-sig")
    {
        int rc = 0;
        for (int i = 2; i < argc; i++)
            rc |= cmd == "files" ? CmdFiles(argv[i]) : (cmd == "awards" ? CmdAwards(argv[i]) : CmdVerifySig(argv[i]));
        return rc;
    }

    Usage();
    return 2;
}
