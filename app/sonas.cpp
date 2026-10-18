#include "sonas.h"

#include "items.h"
#include "../core/avatar_item.h"
#include "../core/profile.h"
#include "../core/sona.h"

#include "xsdk/log.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <set>

static const char* kSonasFolder = "game:\\Sonas";

const char* SonasFolder()
{
    return kSonasFolder;
}

static std::string Lower(const std::string& text)
{
    std::string out(text);
    for (size_t i = 0; i < out.size(); i++)
        out[i] = (char)tolower((unsigned char)out[i]);
    return out;
}

static bool EndsWith(const std::string& text, const char* suffix)
{
    size_t n = strlen(suffix);
    return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

std::vector<std::string> ListSonaFiles()
{
    std::vector<std::string> files;
    std::vector<std::string> names = ListFolder(kSonasFolder, false);
    for (size_t i = 0; i < names.size(); i++)
    {
        std::string lower = Lower(names[i]);
        if (EndsWith(lower, ".txt") || EndsWith(lower, ".sona"))
            files.push_back(names[i]);
    }
    std::sort(files.begin(), files.end());
    return files;
}

// The id an item gets once this app unlocks it: title AAAAAAAA in its last 4 bytes.
static void UnlockedId(const u8* id, u8* out)
{
    memcpy(out, id, kSonaGuidSize);
    out[12] = out[13] = out[14] = out[15] = 0xAA;
}

static bool IsUnlockedId(const u8* id)
{
    return id[12] == 0xAA && id[13] == 0xAA && id[14] == 0xAA && id[15] == 0xAA;
}

static bool ParseIdHex(const std::string& hex, u8* out)
{
    if (hex.size() != 2 * kSonaGuidSize)
        return false;
    for (u32 i = 0; i < 2 * kSonaGuidSize; i++)
    {
        char ch = (char)toupper((unsigned char)hex[i]);
        int v = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
        if (v < 0)
            return false;
        out[i / 2] = (u8)(i % 2 ? (out[i / 2] << 4) | v : v);
    }
    return true;
}

// AvatarItems by full asset id, and by its first 12 bytes for ids that lost
// their title to AAAAAAAA (unlocked on this or another console).
struct SourceIndex
{
    std::map<std::string, std::string> byId;
    std::map<std::string, std::string> idByPrefix;

    explicit SourceIndex(const std::map<std::string, std::string>& sources) : byId(sources)
    {
        for (std::map<std::string, std::string>::const_iterator it = sources.begin(); it != sources.end(); ++it)
            idByPrefix.insert(std::make_pair(it->first.substr(0, 24), it->first));
    }

    // The source package's asset id for `id`, or "" if AvatarItems has none.
    std::string Find(const u8* id) const
    {
        std::string hex = SonaIdHex(id);
        if (byId.count(hex))
            return hex;
        if (IsUnlockedId(id))
        {
            std::map<std::string, std::string>::const_iterator it = idByPrefix.find(hex.substr(0, 24));
            if (it != idByPrefix.end())
                return it->second;
        }
        return "";
    }
};

bool ExportSona(const ProfileEntry& entry, std::string* path, std::string* error)
{
    std::vector<u8> data;
    std::vector<u8> manifest;
    if (entry.signedIn || entry.path.empty() || !ReadWholeFile(entry.path, &data) || data.empty())
    {
        *error = entry.signedIn ? "sign " + entry.info.gamertag + " out first" : "cannot read the profile";
        return false;
    }
    if (!ProfileReadAvatar(&data[0], (u32)data.size(), &manifest, error))
        return false;

    SourceIndex sources(IndexItemSources());
    std::vector<std::vector<u8> > ids = SonaItemIds(&manifest[0]);
    int restored = 0;
    for (size_t i = 0; i < ids.size(); i++)
    {
        if (!IsUnlockedId(&ids[i][0]))
            continue;
        u8 real[kSonaGuidSize];
        std::string hex = sources.Find(&ids[i][0]);
        if (!hex.empty() && ParseIdHex(hex, real))
            restored += SonaReplaceId(&manifest[0], &ids[i][0], real) > 0;
    }

    std::string name;
    for (size_t i = 0; i < entry.info.gamertag.size(); i++)
    {
        char ch = entry.info.gamertag[i];
        name += isalnum((unsigned char)ch) || ch == ' ' || ch == '-' ? ch : '_';
    }
    if (name.empty())
        name = "Sona";
    *path = std::string(kSonasFolder) + "\\" + name + ".txt";
    for (int n = 2; PathExists(*path); n++)
    {
        char suffix[16];
        snprintf(suffix, sizeof suffix, " %d.txt", n);
        *path = std::string(kSonasFolder) + "\\" + name + suffix;
    }
    std::string code = SonaEncode(&manifest[0]);
    if (!MakeDirs("game:", "Sonas") || !WriteWholeFile(*path, (const u8*)code.data(), (u32)code.size()))
    {
        *error = "cannot write to the Sonas folder";
        return false;
    }
    xsdk_log_write("sona: saved %s for %s, %d item ids restored", path->c_str(), entry.info.gamertag.c_str(), restored);
    return true;
}

// How each item of a description is handled when it is worn here.
struct SonaItem
{
    u8 id[kSonaGuidSize];       // as in the sona code
    u8 target[kSonaGuidSize];   // what the profile's description gets
    std::string unlockPath;     // below AvatarItems; empty if nothing to unlock
    enum { kBuiltIn, kUnlocked, kToUnlock, kMissing } state;
};

static std::vector<SonaItem> Resolve(const std::vector<u8>& manifest, const SourceIndex& sources,
                                     const std::set<std::string>& installed)
{
    std::vector<SonaItem> items;
    std::vector<std::vector<u8> > ids = SonaItemIds(&manifest[0]);
    for (size_t i = 0; i < ids.size(); i++)
    {
        SonaItem item;
        memcpy(item.id, &ids[i][0], kSonaGuidSize);
        memcpy(item.target, item.id, kSonaGuidSize);
        if (SonaIsBuiltIn(item.id))
            item.state = SonaItem::kBuiltIn;
        else
        {
            UnlockedId(item.id, item.target);
            std::string source = sources.Find(item.id);
            if (installed.count(SonaIdHex(item.target)))
                item.state = SonaItem::kUnlocked;
            else if (!source.empty())
            {
                item.state = SonaItem::kToUnlock;
                item.unlockPath = sources.byId.find(source)->second;
            }
            else
            {
                // Perhaps an award the profile earned, installed by its game:
                // left exactly as the sona has it.
                item.state = SonaItem::kMissing;
                memcpy(item.target, item.id, kSonaGuidSize);
            }
        }
        items.push_back(item);
    }
    return items;
}

static bool ReadSonaFile(const std::string& file, std::vector<u8>* manifest, std::string* error)
{
    std::vector<u8> bytes;
    if (!ReadWholeFile(std::string(kSonasFolder) + "\\" + file, &bytes) || bytes.empty())
    {
        *error = "cannot read " + file;
        return false;
    }
    return SonaDecode(std::string(bytes.begin(), bytes.end()), manifest, error);
}

bool PlanSona(const std::string& file, const std::vector<Drive>& drives, SonaPlan* plan, std::string* error)
{
    plan->file = file;
    plan->builtIn = 0;
    plan->unlocked = 0;
    plan->toUnlock.clear();
    plan->missing.clear();
    if (!ReadSonaFile(file, &plan->manifest, error))
        return false;
    std::vector<SonaItem> items = Resolve(plan->manifest, SourceIndex(IndexItemSources()), InstalledItemNames(drives));
    for (size_t i = 0; i < items.size(); i++)
    {
        if (items[i].state == SonaItem::kBuiltIn)
            plan->builtIn++;
        else if (items[i].state == SonaItem::kUnlocked)
            plan->unlocked++;
        else if (items[i].state == SonaItem::kToUnlock)
            plan->toUnlock.push_back(items[i].unlockPath);
        else
            plan->missing.push_back(SonaIdHex(items[i].id));
    }
    return true;
}

bool WearSona(const ProfileEntry& entry, const SonaPlan& plan, const std::vector<u64>& licensees,
              const std::vector<Drive>& drives, const Drive& drive, std::string* backupPath, int* failed,
              std::string* error)
{
    *failed = 0;
    std::vector<u8> data;
    ProfileInfo info;
    std::vector<u8> current;
    if (entry.path.empty() || !ReadWholeFile(entry.path, &data) || data.empty() ||
        !ProfileInspect(&data[0], (u32)data.size(), &info, error))
    {
        if (error->empty()) *error = "cannot read the profile";
        return false;
    }
    if (IsProfileSignedIn(info))
    {
        *error = "sign " + info.gamertag + " out first";
        return false;
    }
    if (!ProfileReadAvatar(&data[0], (u32)data.size(), &current, error))
        return false;

    // Worked out again here: the drives may have changed since the plan.
    std::vector<u8> manifest = plan.manifest;
    std::vector<SonaItem> items = Resolve(manifest, SourceIndex(IndexItemSources()), InstalledItemNames(drives));
    for (size_t i = 0; i < items.size(); i++)
    {
        SonaItem& item = items[i];
        if (item.state == SonaItem::kToUnlock)
        {
            std::string why;
            if (!InstallItemFile(item.unlockPath, false, licensees, drive, &why))
            {
                xsdk_log_write("sona: unlocking %s failed: %s", item.unlockPath.c_str(), why.c_str());
                (*failed)++;
                continue;
            }
        }
        SonaReplaceId(&manifest[0], item.id, item.target);
    }

    // The description carries the XUID of the profile it belongs to; that one stays.
    memcpy(&manifest[kSonaOffsetOwner], &current[kSonaOffsetOwner], 8);

    if (!BackupProfile(entry, data, backupPath, error))
        return false;
    if (!ProfileWriteAvatar(&data[0], (u32)data.size(), &manifest[0], error) || !SignProfile(&data, error))
        return false;
    if (!WriteWholeFile(entry.path, &data[0], (u32)data.size()))
    {
        *error = "writing the profile failed; restore it from " + *backupPath;
        return false;
    }
    xsdk_log_write("sona: %s now wears %s (%d unlock failures), backup %s", info.gamertag.c_str(), plan.file.c_str(),
                   *failed, backupPath->c_str());
    return true;
}
