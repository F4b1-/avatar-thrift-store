#include "profiles.h"

#include "kernel_extra.h"
#include "storage.h"
#include "../core/avatar_item.h"
#include "../core/stfs.h"

#include "xsdk/log.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

// Profiles live in Content\<offline xuid>\FFFE07D1\00010000\<offline xuid>.
static const char* kProfileSubfolder = "\\FFFE07D1\\00010000\\";

static bool IsOfflineXuidName(const std::string& name)
{
    if (name.size() != 16 || name[0] != 'E')
        return false;
    for (size_t i = 0; i < name.size(); i++)
        if (!isxdigit((unsigned char)name[i]))
            return false;
    return true;
}

// What the system reports for a signed-in user.
struct SignedInInfo
{
    std::string gamertag;
    u64 offlineXuid;    // E..., the one the profile's folder is named after
    u64 onlineXuid;     // 0 if it has none
};

// Asks with both XUID flags, 1 and 2, and sorts the answers by range: E... is
// the offline XUID, anything else non-zero the online one. Which flag asks for
// which isn't documented anywhere we could check (Xenia ignores the flags), and
// taking flag 1 for the offline XUID made a signed-in activated profile miss
// its own folder. False if the user isn't signed in.
static bool GetSignedInInfo(u32 user, SignedInInfo* out)
{
    out->gamertag.clear();
    out->offlineXuid = 0;
    out->onlineXuid = 0;
    bool signedIn = false;
    for (u32 flags = 1; flags <= 2; flags++)
    {
        XUserSigninInfo info;
        memset(&info, 0, sizeof info);
        if (XamUserGetSigninInfo(user, flags, &info) != 0 || info.signinState == 0)
            continue;
        signedIn = true;
        info.name[sizeof info.name - 1] = 0;
        out->gamertag = info.name;
        if ((info.xuid >> 60) == 0xE)
            out->offlineXuid = info.xuid;
        else if (IsOnlineXuid(info.xuid))
            out->onlineXuid = info.xuid;
    }
    return signedIn;
}

// Matches the XUID or the gamertag, so a profile in use is caught even if one
// of the two isn't what the console reports.
static bool IsSignedIn(u64 offlineXuid, const std::string& gamertag)
{
    for (u32 user = 0; user < 4; user++)
    {
        SignedInInfo info;
        if (GetSignedInInfo(user, &info) && (info.offlineXuid == offlineXuid || info.gamertag == gamertag))
            return true;
    }
    return false;
}

// The signed-in user with this offline XUID: its gamertag, and the online XUID
// the system reports for it (0 if none). False if it isn't signed in.
static bool SignedInUser(u64 offlineXuid, std::string* gamertag, u64* onlineXuid)
{
    for (u32 user = 0; user < 4; user++)
    {
        SignedInInfo info;
        if (!GetSignedInInfo(user, &info) || info.offlineXuid != offlineXuid)
            continue;
        *gamertag = info.gamertag;
        *onlineXuid = info.onlineXuid;
        return true;
    }
    return false;
}

// Fills an entry with what the system reports for a signed-in profile, for
// when its file can't be read; false if that profile isn't signed in.
static bool FillFromSystem(ProfileEntry* entry, u64 offlineXuid)
{
    entry->info.offlineXuid = offlineXuid;
    entry->info.onlineXuid = 0;
    entry->info.reservedFlags = 0;
    entry->info.signatureValid = true;
    entry->info.kind = kProfileOffline;
    if (!SignedInUser(offlineXuid, &entry->info.gamertag, &entry->info.onlineXuid))
        return false;
    entry->signedIn = true;
    if (entry->info.onlineXuid != 0)
        entry->info.kind = entry->info.onlineXuid == kSharedOnlineXuid ? kProfileActivated : kProfileLive;
    return true;
}

static bool ContainsIgnoringCase(const std::vector<std::string>& names, const std::string& name)
{
    for (size_t i = 0; i < names.size(); i++)
        if (names[i].size() == name.size() && strncasecmp(names[i].c_str(), name.c_str(), name.size()) == 0)
            return true;
    return false;
}

std::vector<ProfileEntry> ScanProfiles()
{
    std::vector<ProfileEntry> profiles;
    std::vector<u64> folderXuids;   // each listed profile's folder name, as a number
    std::vector<Drive> drives = AvailableDrives();
    for (size_t d = 0; d < drives.size(); d++)
    {
        std::string content = drives[d].root + "\\Content";
        std::vector<std::string> owners = ListFolder(content, true);
        for (size_t i = 0; i < owners.size(); i++)
        {
            if (!IsOfflineXuidName(owners[i]))
                continue;
            // Found by listing its folder, not by asking for the file: the system
            // holds a signed-in profile's file open, and then even a query of its
            // attributes (stat) can fail.
            std::string folder = content + "\\" + owners[i] + kProfileSubfolder;
            if (!ContainsIgnoringCase(ListFolder(folder, false), owners[i]))
                continue;   // a folder of saves without a profile
            ProfileEntry entry;
            entry.drive = drives[d].name;
            entry.path = folder + owners[i];
            entry.signedIn = false;
            u64 offlineXuid = 0;
            ParseHex64(owners[i].c_str(), &offlineXuid);
            folderXuids.push_back(offlineXuid);
            std::vector<u8> data;
            if (ReadWholeFile(entry.path, &data) && !data.empty())
            {
                if (ProfileInspect(&data[0], (u32)data.size(), &entry.info, &entry.error))
                    entry.signedIn = IsSignedIn(entry.info.offlineXuid, entry.info.gamertag) ||
                                     IsSignedIn(offlineXuid, entry.info.gamertag);
                else
                    entry.info.gamertag = owners[i];
                profiles.push_back(entry);
                continue;
            }
            if (!FillFromSystem(&entry, offlineXuid))
            {
                entry.info.gamertag = owners[i];
                entry.error = "cannot read the profile";
            }
            xsdk_log_write("profiles: %s can't be read%s", entry.path.c_str(), entry.signedIn ? " (signed in)" : "");
            profiles.push_back(entry);
        }
    }

    // Signed-in profiles the scan didn't find (stored somewhere it doesn't look)
    // are listed from what the system reports.
    for (u32 user = 0; user < 4; user++)
    {
        SignedInInfo info;
        if (!GetSignedInInfo(user, &info))
            continue;
        xsdk_log_write("profiles: user %u signed in: %s, offline %016llX, online %016llX", (unsigned)user,
                       info.gamertag.c_str(), (unsigned long long)info.offlineXuid, (unsigned long long)info.onlineXuid);
        // Already listed if its folder, its header's XUID or its gamertag matches.
        bool listed = false;
        for (size_t i = 0; i < profiles.size() && !listed; i++)
            listed = (info.offlineXuid != 0 && (folderXuids[i] == info.offlineXuid ||
                                                profiles[i].info.offlineXuid == info.offlineXuid)) ||
                     profiles[i].info.gamertag == info.gamertag;
        if (listed || info.offlineXuid == 0)
            continue;
        ProfileEntry entry;
        entry.drive = "Signed in";
        entry.signedIn = false;
        if (FillFromSystem(&entry, info.offlineXuid))
        {
            xsdk_log_write("profiles: signed-in %s not found on the drives", entry.info.gamertag.c_str());
            profiles.push_back(entry);
            folderXuids.push_back(info.offlineXuid);
        }
    }
    return profiles;
}

std::vector<u64> SignedInOnlineXuids()
{
    std::vector<u64> xuids;
    for (u32 user = 0; user < 4; user++)
    {
        SignedInInfo info;
        if (GetSignedInInfo(user, &info) && info.onlineXuid != 0)
            xuids.push_back(info.onlineXuid);
    }
    return xuids;
}

int FirstSignedInUser(std::string* gamertag)
{
    for (u32 user = 0; user < 4; user++)
    {
        SignedInInfo info;
        if (!GetSignedInInfo(user, &info))
            continue;
        *gamertag = info.gamertag;
        return (int)user;
    }
    return -1;
}

u32 LaunchAvatarEditor(int user)
{
    xsdk_log_write("avatar editor: launching for user %d", user);
    u32 result = XamLaunchAvatarEditor((u32)user, 0, 0);
    xsdk_log_write("avatar editor: XamLaunchAvatarEditor returned 0x%08X", (unsigned)result);
    return result;
}

static std::string BackupPath(const ProfileEntry& entry)
{
    char xuid[17];
    snprintf(xuid, sizeof xuid, "%016llX", (unsigned long long)entry.info.offlineXuid);
    for (int n = 1;; n++)
    {
        char name[64];
        snprintf(name, sizeof name, "game:\\Backups\\%s_%d", xuid, n);
        if (!PathExists(name))
            return name;
    }
}

bool IsProfileSignedIn(const ProfileInfo& info)
{
    return IsSignedIn(info.offlineXuid, info.gamertag);
}

bool BackupProfile(const ProfileEntry& entry, const std::vector<u8>& data, std::string* backupPath, std::string* error)
{
    *backupPath = BackupPath(entry);
    if (data.empty() || !MakeDirs("game:", "Backups") || !WriteWholeFile(*backupPath, &data[0], (u32)data.size()))
    {
        *error = "cannot write the backup, nothing was changed";
        return false;
    }
    return true;
}

bool SignProfile(std::vector<u8>* data, std::string* error)
{
    u8 hash[20];
    StfsComputeSignedHash(&(*data)[0], hash);
    // The kernel writes certificate and signature here; keep it 8-byte aligned.
    static u64 signature[(kStfsConsoleSignatureSize + 7) / 8];
    memset(signature, 0, sizeof signature);
    // What the return value means isn't confirmed on hardware, so it only goes
    // to the log; whether the signature verifies is what counts.
    u32 result = XeKeysConsolePrivateKeySign(hash, (u8*)signature);
    const u8* s = (const u8*)signature;
    bool empty = true;
    for (u32 i = 0; i < kStfsConsoleSignatureSize && empty; i++)
        empty = s[i] == 0;
    xsdk_log_write("sign: XeKeysConsolePrivateKeySign returned 0x%08X; cert %02X%02X%02X%02X, signature %02X%02X%02X%02X",
                   (unsigned)result, s[0], s[1], s[2], s[3], s[0x1A8], s[0x1A9], s[0x1AA], s[0x1AB]);
    if (empty)
    {
        char code[16];
        snprintf(code, sizeof code, "0x%08X", (unsigned)result);
        *error = std::string("the console returned no signature (") + code + "), nothing was changed";
        return false;
    }
    if (!ProfileApplyConsoleSignature(&(*data)[0], (u32)data->size(), (const u8*)signature, error))
    {
        *error += ", nothing was changed";
        return false;
    }
    return true;
}

bool ActivateProfile(const ProfileEntry& entry, std::string* backupPath, std::string* error)
{
    // Read it again: the list may be older than the file.
    std::vector<u8> data;
    ProfileInfo info;
    if (!ReadWholeFile(entry.path, &data) || data.empty() ||
        !ProfileInspect(&data[0], (u32)data.size(), &info, error))
    {
        if (error->empty()) *error = "cannot read the profile";
        return false;
    }
    if (info.kind != kProfileOffline)
    {
        *error = info.kind == kProfileActivated ? "the profile is already activated" : "this is an Xbox Live profile";
        return false;
    }
    if (IsSignedIn(info.offlineXuid, info.gamertag))
    {
        *error = "sign " + info.gamertag + " out first";
        return false;
    }

    if (!BackupProfile(entry, data, backupPath, error))
        return false;

    if (!ProfileSetOnlineXuid(&data[0], (u32)data.size(), kSharedOnlineXuid, true, error) || !SignProfile(&data, error))
        return false;

    if (!WriteWholeFile(entry.path, &data[0], (u32)data.size()))
    {
        *error = "writing the profile failed; restore it from " + *backupPath;
        return false;
    }
    return true;
}
