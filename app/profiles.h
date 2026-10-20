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
// The first signed-in offline profile that isn't activated yet; false if there
// is none, or its file isn't on the drives (then it can't be activated).
bool FindSignedInOfflineProfile(ProfileEntry* out);

// Signs a signed-in profile out and waits until the system lets go of its
// file, so ActivateProfile can rewrite it.
bool SignOutProfile(const ProfileEntry& entry, std::string* error);

// Activates an offline profile in place. A copy of the original goes to
// game:\Backups first; nothing is written unless the console's signature
// verifies. `backupPath` names the copy.
bool ActivateProfile(const ProfileEntry& entry, std::string* backupPath, std::string* error);
