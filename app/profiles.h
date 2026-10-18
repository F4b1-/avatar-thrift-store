// Profiles on the hard disk and USB storage, and activating an offline one:
// the shared online XUID, Live-flagged as 360mpGui's maker does, rehashed and
// signed with this console's own key.
#pragma once

#include "../core/profile.h"

#include <string>
#include <vector>

struct ProfileEntry
{
    std::string drive;      // "HDD", "USB 1", ...
    std::string path;       // ...\Content\<xuid>\FFFE07D1\00010000\<xuid>
    ProfileInfo info;
    bool signedIn;
    std::string error;      // when the file can't be read as a profile
};

// Every profile on the hard disk and USB storage. A signed-in profile's file is
// held open by the system; such a profile is still listed, with its gamertag
// and online XUID as the system reports them.
std::vector<ProfileEntry> ScanProfiles();

// The online XUIDs the system reports for the users signed in now.
std::vector<u64> SignedInOnlineXuids();

// The first signed-in user (0-3) and its gamertag; -1 if nobody is signed in.
int FirstSignedInUser(std::string* gamertag);

// Asks the system to open the avatar editor for `user`, the way the dashboard
// does; the system then ends this app. Returns the system's answer (logged).
u32 LaunchAvatarEditor(int user);
// Activates an offline profile in place. A copy of the original goes to
// game:\Backups first; nothing is written unless the console's signature
// verifies. `backupPath` names the copy.
bool ActivateProfile(const ProfileEntry& entry, std::string* backupPath, std::string* error);

// True if the profile is signed in now (its file is then held by the system).
bool IsProfileSignedIn(const ProfileInfo& info);

// Copies the profile's current contents to game:\Backups.
bool BackupProfile(const ProfileEntry& entry, const std::vector<u8>& data, std::string* backupPath, std::string* error);

// Signs a changed profile with this console's own key. False, and `data`
// unchanged, unless the signature verifies.
bool SignProfile(std::vector<u8>* data, std::string* error);
