// 360sona on the console: a profile's avatar saved as a sona code, and a sona
// code worn by a profile (see core/sona.h for the format).
//
// Sona codes live in the Sonas folder next to the app, as .txt or .sona files.
// 360sona names items by their real asset ids, while items unlocked by this app
// are installed under title AAAAAAAA (the last 4 bytes of the asset id). So:
//   - saving puts the real ids back for unlocked items whose package is in
//     AvatarItems (360sona finds the rest by the first 12 bytes);
//   - wearing points each item at its unlocked copy, unlocking it first from
//     AvatarItems when the package is there.
#pragma once

#include "profiles.h"
#include "storage.h"

#include <string>
#include <vector>

// "game:\Sonas"
const char* SonasFolder();

// The sona codes in the Sonas folder, sorted by name.
std::vector<std::string> ListSonaFiles();

// Writes the profile's avatar as game:\Sonas\<gamertag>.txt (a numbered name if
// that exists). `path` names the file.
bool ExportSona(const ProfileEntry& entry, std::string* path, std::string* error);

// What wearing a sona would do, worked out before anything is written.
struct SonaPlan
{
    std::string file;                   // below the Sonas folder
    std::vector<u8> manifest;           // as read; the remapping happens in WearSona
    int builtIn;                        // on every console
    int unlocked;                       // already unlocked on a drive
    std::vector<std::string> toUnlock;  // paths below AvatarItems
    std::vector<std::string> missing;   // asset ids found in neither place
};

bool PlanSona(const std::string& file, const std::vector<Drive>& drives, SonaPlan* plan, std::string* error);

// Unlocks the plan's items to `drive`, then writes the avatar into the profile
// (kept: the profile's own XUID inside the description), backed up first and
// signed with this console's key. `failed` counts items that couldn't be
// unlocked; they are left as the sona had them.
bool WearSona(const ProfileEntry& entry, const SonaPlan& plan, const std::vector<u64>& licensees,
              const std::vector<Drive>& drives, const Drive& drive, std::string* backupPath, int* failed,
              std::string* error);
