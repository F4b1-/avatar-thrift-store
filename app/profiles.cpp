#include "profiles.h"

#include "kernel_extra.h"
#include "storage.h"
#include "../core/avatar_item.h"
#include "../core/stfs.h"

#include "xsdk/log.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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

// Matches the XUID or the gamertag, so a profile in use is caught even if one
// of the two isn't what the console reports.
static bool IsSignedIn(u64 offlineXuid, const std::string& gamertag)
{
    for (u32 user = 0; user < 4; user++)
    {
        XUserSigninInfo info;
        memset(&info, 0, sizeof info);
        if (XamUserGetSigninInfo(user, XUSER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY, &info) != 0 || info.signinState == 0)
            continue;
        info.name[sizeof info.name - 1] = 0;
        if (info.xuid == offlineXuid || gamertag == info.name)
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
        XUserSigninInfo info;
        memset(&info, 0, sizeof info);
        if (XamUserGetSigninInfo(user, XUSER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY, &info) != 0 || info.signinState == 0 ||
            info.xuid != offlineXuid)
            continue;
        info.name[sizeof info.name - 1] = 0;
        *gamertag = info.name;
        XUserSigninInfo online;
        memset(&online, 0, sizeof online);
        bool known = XamUserGetSigninInfo(user, XUSER_GET_SIGNIN_INFO_ONLINE_XUID_ONLY, &online) == 0 &&
                     online.xuid != offlineXuid && IsOnlineXuid(online.xuid);
        *onlineXuid = known ? online.xuid : 0;
        return true;
    }
    return false;
}

std::vector<ProfileEntry> ScanProfiles()
{
    std::vector<ProfileEntry> profiles;
    std::vector<Drive> drives = AvailableDrives();
    for (size_t d = 0; d < drives.size(); d++)
    {
        std::string content = drives[d].root + "\\Content";
        std::vector<std::string> owners = ListFolder(content, true);
        for (size_t i = 0; i < owners.size(); i++)
        {
            if (!IsOfflineXuidName(owners[i]))
                continue;
            ProfileEntry entry;
            entry.drive = drives[d].name;
            entry.path = content + "\\" + owners[i] + kProfileSubfolder + owners[i];
            entry.signedIn = false;
            if (!PathExists(entry.path))
                continue;   // a folder of saves without a profile
            std::vector<u8> data;
            if (ReadWholeFile(entry.path, &data) && !data.empty())
            {
                if (ProfileInspect(&data[0], (u32)data.size(), &entry.info, &entry.error))
                    entry.signedIn = IsSignedIn(entry.info.offlineXuid, entry.info.gamertag);
                else
                    entry.info.gamertag = owners[i];
                profiles.push_back(entry);
                continue;
            }

            // The file of a signed-in profile is held open by the system; what
            // matters then comes from the system instead.
            u64 offlineXuid = 0;
            ParseHex64(owners[i].c_str(), &offlineXuid);
            entry.info.offlineXuid = offlineXuid;
            entry.info.onlineXuid = 0;
            entry.info.reservedFlags = 0;
            entry.info.signatureValid = true;
            entry.info.kind = kProfileOffline;
            if (SignedInUser(offlineXuid, &entry.info.gamertag, &entry.info.onlineXuid))
            {
                entry.signedIn = true;
                if (entry.info.onlineXuid != 0)
                    entry.info.kind = entry.info.onlineXuid == kSharedOnlineXuid ? kProfileActivated : kProfileLive;
            }
            else
            {
                entry.info.gamertag = owners[i];
                entry.error = "cannot read the profile";
            }
            profiles.push_back(entry);
        }
    }
    return profiles;
}

std::vector<u64> SignedInOnlineXuids()
{
    std::vector<u64> xuids;
    for (u32 user = 0; user < 4; user++)
    {
        XUserSigninInfo info;
        memset(&info, 0, sizeof info);
        if (XamUserGetSigninInfo(user, XUSER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY, &info) != 0 || info.signinState == 0)
            continue;
        std::string gamertag;
        u64 online = 0;
        if (SignedInUser(info.xuid, &gamertag, &online) && online != 0)
            xuids.push_back(online);
    }
    return xuids;
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

    *backupPath = BackupPath(entry);
    if (!MakeDirs("game:", "Backups") || !WriteWholeFile(*backupPath, &data[0], (u32)data.size()))
    {
        *error = "cannot write the backup, nothing was changed";
        return false;
    }

    if (!ProfileSetOnlineXuid(&data[0], (u32)data.size(), kSharedOnlineXuid, true, error))
        return false;

    u8 hash[20];
    StfsComputeSignedHash(&data[0], hash);
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
    xsdk_log_write("activate: XeKeysConsolePrivateKeySign returned 0x%08X; cert %02X%02X%02X%02X, signature %02X%02X%02X%02X",
                   (unsigned)result, s[0], s[1], s[2], s[3], s[0x1A8], s[0x1A9], s[0x1AA], s[0x1AB]);
    if (empty)
    {
        char code[16];
        snprintf(code, sizeof code, "0x%08X", (unsigned)result);
        *error = std::string("the console returned no signature (") + code + "), nothing was changed";
        return false;
    }
    if (!ProfileApplyConsoleSignature(&data[0], (u32)data.size(), (const u8*)signature, error))
    {
        *error += ", nothing was changed";
        return false;
    }

    if (!WriteWholeFile(entry.path, &data[0], (u32)data.size()))
    {
        *error = "writing the profile failed; restore it from " + *backupPath;
        return false;
    }
    return true;
}
