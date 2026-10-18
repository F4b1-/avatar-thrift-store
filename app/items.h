// Avatar item packages in the AvatarItems folder next to the app (subfolders
// included), and unlocking them for every profile on the console: patched to
// title AAAAAAAA, licensed to the shared online XUID (activated offline
// profiles) and to each Xbox Live profile's own online XUID, and copied to
// Content\0000000000000000\AAAAAAAA\00009000 on the hard disk or a USB stick.
// `drives` are the ones items can be installed to (AvailableDrives).
#pragma once

#include "storage.h"
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
    std::vector<u32> icon;      // kIconSize x kIconSize ARGB, empty without one
    bool installed;             // and licensed to every one of the licensees
    std::vector<std::string> installedOn;   // the drives with such a copy ("HDD", "USB 1")
    // Listing reads only the header; the preview, the full check and the
    // license check come with LoadItemDetails.
    bool detailsLoaded;
    std::string installedName;  // its file name in the Content folder
};

// One folder's contents, each sorted by name.
struct ItemListing
{
    std::vector<std::string> folders;
    std::vector<Item> items;
};

// "game:\AvatarItems"
const char* ItemsFolder();

// The XUIDs items are licensed to: the shared online XUID first, then the
// online XUIDs of the Xbox Live profiles (from their files, and from the
// system for the signed-in ones, whose files are held open).
std::vector<u64> ItemLicensees();

// Reads a folder below AvatarItems ("" for AvatarItems itself): the start of
// each item's header only, so large folders open quickly. `installed` means a
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
// it to Content on `drive`, keeping XUIDs the copy there already had; with
// `removeSource` the file is deleted once that worked.
bool InstallItemFile(const std::string& path, bool removeSource, const std::vector<u64>& licensees,
                     const Drive& drive, std::string* error);

struct InstallAllResult
{
    int installed;
    int failed;
    std::vector<std::string> errors;   // the first few, "name: reason"
};

// Installs every file in the folder and its subfolders to `drive`.
InstallAllResult InstallAllItems(const std::string& relative, bool removeSource, const std::vector<u64>& licensees,
                                 const Drive& drive);

// Adds the licensees missing from the items already installed on any of the
// drives, so profiles found later get them too, even when the source files are
// gone. `installed` counts the items that were changed.
InstallAllResult UpdateInstalledItems(const std::vector<u64>& licensees, const std::vector<Drive>& drives);
