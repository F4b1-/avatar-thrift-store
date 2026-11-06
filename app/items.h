// Avatar item packages in the AvatarItems folder next to the app (subfolders
// included), and unlocking them for every profile on the console: patched to
// title AAAAAAAA, licensed to the shared online XUID (activated offline
// profiles) and to each Xbox Live profile's own online XUID, and copied to
// Content\0000000000000000\AAAAAAAA\00009000 on the hard disk or a USB stick.
// `drives` are the ones items can be installed to (AvailableDrives).
#pragma once

#include "profiles.h"
#include "storage.h"
#include "../core/avatar_item.h"
#include "../core/types.h"

#include <string>
#include <vector>

// Item previews: the package's icon.png, 128 x 128.
enum { kIconSize = 128 };

struct Item
{
    std::string path;           // below AvatarItems: "Domo Pack\\00000008..."
    std::string name;           // the package's display name, or its file name
    std::string error;          // why it can't be installed, if it can't
    AvatarBodyType body;        // male or female item, from the header
    std::vector<u32> icon;      // kIconSize x kIconSize ARGB, empty without one
    bool installed;             // and licensed to every one of the licensees
    std::vector<std::string> installedOn;   // the drives with such a copy ("HDD", "USB 1")
    // Listing reads only the header; the preview, the full check and the
    // license check come with LoadItemDetails.
    bool detailsLoaded;
    std::string installedName;  // its file name in the Content folder
};

// One folder's contents, each sorted by name (items of the same name male
// before female).
struct ItemListing
{
    std::vector<std::string> folders;
    std::vector<Item> items;
};

// "game:\AvatarItems"
const char* ItemsFolder();

// The XUIDs items are licensed to: the shared online XUID first, then the
// online XUIDs of the Xbox Live profiles (from their files in `profiles`, a
// ScanProfiles result, and from the system for the signed-in ones, whose files
// are held open).
std::vector<u64> ItemLicensees(const std::vector<ProfileEntry>& profiles);

// Reads a folder below AvatarItems ("" for AvatarItems itself): the start of
// each item's header only, so large folders open quickly. Files that aren't
// avatar item packages are left out. `installed` means a
// copy is in a Content folder until LoadItemDetails checks its license.
ItemListing ScanItemFolder(const std::string& relative, const std::vector<Drive>& drives);

// Reads the whole package: checks it, decodes its preview and checks the
// installed copies' licenses against `licensees`. Does nothing the second time.
void LoadItemDetails(Item* item, const std::vector<u64>& licensees, const std::vector<Drive>& drives);

// Every folder below AvatarItems, at any depth, as relative paths.
std::vector<std::string> IndexItemFolders();

// The folders whose names contain every space-separated word of `query`,
// ignoring case, sorted by name.
std::vector<std::string> SearchFolders(const std::vector<std::string>& index, const std::string& query);

// The last part of a relative path: "Games\\Halo 3" -> "Halo 3".
std::string FolderName(const std::string& relative);

// Patches the package at `path` (below AvatarItems) for the licensees and writes
// it to Content on `drive`, keeping XUIDs the copy there already had. The file
// itself stays, so unlocking it again adds profiles found since.
bool InstallItemFile(const std::string& path, const std::vector<u64>& licensees, const Drive& drive,
                     std::string* error);

struct InstallAllResult
{
    int installed;
    int failed;
    std::vector<std::string> errors;   // the first few, "name: reason"
};

// Installs every item in a folder and its subfolders to `drive`, one step at a
// time, so the app can show progress and stop between steps: first one folder
// is listed per step, then one file is unlocked per step. Each file is written
// completely within its step, so stopping between steps leaves no half-written
// item. Progress goes to the log every few dozen files.
class InstallAllJob
{
public:
    InstallAllJob();
    void Start(const std::string& relative, const std::vector<u64>& licensees, const Drive& drive);
    void Step();
    bool Finished() const { return m_folders.empty() && m_next >= m_files.size(); }
    bool Listing() const { return !m_folders.empty(); }     // still finding the files
    int Files() const { return (int)m_files.size(); }       // found so far
    int Done() const { return (int)m_next; }                // files looked at
    const InstallAllResult& Result() const { return m_result; }

private:
    std::vector<u64> m_licensees;
    Drive m_drive;
    std::vector<std::string> m_folders;     // still to list; the next one last
    std::vector<std::string> m_files;       // below AvatarItems, in the order they're unlocked
    size_t m_next;
    InstallAllResult m_result;
};
