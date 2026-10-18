#include "items.h"

#include "profiles.h"
#include "storage.h"
#include "../core/account.h"
#include "../core/avatar_item.h"
#include "../core/stfs.h"
#include "../core/stfs_package.h"

#include "stb_image.h"

#include <ctype.h>

#include <algorithm>
#include <set>

static const char* kItemsFolder = "game:\\AvatarItems";
static const size_t kMaxReportedErrors = 3;
// Enough of a package for its type, display name and asset id.
static const u32 kItemHeaderBytes = 0x1800;

const char* ItemsFolder()
{
    return kItemsFolder;
}

static std::string Join(const std::string& relative, const std::string& name)
{
    return relative.empty() ? name : relative + "\\" + name;
}

static std::string FullPath(const std::string& relative)
{
    return relative.empty() ? kItemsFolder : std::string(kItemsFolder) + "\\" + relative;
}

static std::string ContentFolder()
{
    return "Content\\0000000000000000\\" + AvatarItemContentSubfolder(kAvatarDefaultTitleId);
}

static std::string InstalledFolder(const Drive& drive)
{
    return drive.root + "\\" + ContentFolder() + "\\";
}

// The online XUIDs an installed item is licensed to (none if it isn't installed).
// Only the license entries are read.
static std::vector<u64> InstalledLicensees(const std::string& path)
{
    std::vector<u64> xuids;
    std::vector<u8> header;
    const u32 bytes = kStfsOffsetLicenses + kStfsLicenseCount * 16;
    if (!ReadFilePrefix(path, bytes, &header) || header.size() != bytes)
        return xuids;
    for (int i = 1; i < kStfsLicenseCount; i++)
    {
        u64 xuid = 0;
        for (int b = 0; b < 8; b++)
            xuid = xuid << 8 | header[kStfsOffsetLicenses + i * 16 + b];
        if (IsOnlineXuid(xuid))
            xuids.push_back(xuid);
    }
    return xuids;
}

static bool InstalledFor(const std::string& path, const std::vector<u64>& licensees)
{
    std::vector<u64> xuids = InstalledLicensees(path);
    if (xuids.empty())
        return false;
    for (size_t i = 0; i < licensees.size(); i++)
        if (std::find(xuids.begin(), xuids.end(), licensees[i]) == xuids.end())
            return false;
    return true;
}

static void AddUnique(std::vector<u64>* xuids, u64 xuid)
{
    if (IsOnlineXuid(xuid) && std::find(xuids->begin(), xuids->end(), xuid) == xuids->end())
        xuids->push_back(xuid);
}

std::vector<u64> ItemLicensees()
{
    std::vector<u64> licensees(1, kSharedOnlineXuid);
    std::vector<ProfileEntry> profiles = ScanProfiles();
    for (size_t i = 0; i < profiles.size(); i++)
        if (profiles[i].error.empty() && profiles[i].info.kind == kProfileLive)
            AddUnique(&licensees, profiles[i].info.onlineXuid);
    std::vector<u64> signedIn = SignedInOnlineXuids();
    for (size_t i = 0; i < signedIn.size(); i++)
        AddUnique(&licensees, signedIn[i]);
    return licensees;
}

// Entry [1] gets the shared XUID, the rest go into free entries.
static AvatarPatchOptions PatchOptions(const std::vector<u64>& licensees)
{
    AvatarPatchOptions options;
    options.newTitleId = kAvatarDefaultTitleId;
    options.setXuid = true;
    options.xuid = kSharedOnlineXuid;
    options.extraXuids = licensees;
    return options;
}

std::string FolderName(const std::string& relative)
{
    size_t slash = relative.rfind('\\');
    return slash == std::string::npos ? relative : relative.substr(slash + 1);
}

static std::string Lower(const std::string& text)
{
    std::string out(text);
    for (size_t i = 0; i < out.size(); i++)
        out[i] = (char)tolower((unsigned char)out[i]);
    return out;
}

static bool LessIgnoringCase(const std::string& a, const std::string& b)
{
    return Lower(a) < Lower(b);
}

static bool ItemLess(const Item& a, const Item& b)
{
    std::string la = Lower(a.name);
    std::string lb = Lower(b.name);
    return la != lb ? la < lb : a.path < b.path;
}

static bool FolderPathLess(const std::string& a, const std::string& b)
{
    std::string na = Lower(FolderName(a));
    std::string nb = Lower(FolderName(b));
    return na != nb ? na < nb : Lower(a) < Lower(b);
}

// A PNG scaled to kIconSize (nearest pixel), as ARGB.
static bool DecodeIcon(const u8* png, u32 bytes, std::vector<u32>* icon)
{
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* rgba = stbi_load_from_memory(png, (int)bytes, &w, &h, &channels, 4);
    if (!rgba)
        return false;
    icon->resize(kIconSize * kIconSize);
    for (int y = 0; y < kIconSize; y++)
        for (int x = 0; x < kIconSize; x++)
        {
            const unsigned char* p = rgba + ((y * h / kIconSize) * w + (x * w / kIconSize)) * 4;
            (*icon)[y * kIconSize + x] = (u32)p[3] << 24 | (u32)p[0] << 16 | (u32)p[1] << 8 | p[2];
        }
    stbi_image_free(rgba);
    return true;
}

static std::string Upper(const std::string& text)
{
    std::string out(text);
    for (size_t i = 0; i < out.size(); i++)
        out[i] = (char)toupper((unsigned char)out[i]);
    return out;
}

// The first stage, for listing: the header's start only (the display name ends
// at 0x1711), no hash check, no preview. `installed` comes from whether a file
// of the patched name is in a Content folder; LoadItemDetails checks it.
static Item ReadItemHeader(const std::string& relative, const std::string& fileName,
                           const std::set<std::string>& installedNames)
{
    Item item;
    item.path = Join(relative, fileName);
    item.name = fileName;
    item.installed = false;
    item.detailsLoaded = false;

    std::vector<u8> header;
    StfsHeader h;
    if (!ReadFilePrefix(FullPath(item.path), kItemHeaderBytes, &header) ||
        !StfsParseHeader(header.empty() ? NULL : &header[0], (u32)header.size(), &h, &item.error))
    {
        if (item.error.empty()) item.error = "cannot read the file";
        item.detailsLoaded = true;   // nothing more to load
        return item;
    }
    if (h.contentType != kContentTypeAvatarItem)
    {
        item.error = std::string("not an avatar item (content type: ") + StfsContentTypeName(h.contentType) + ")";
        item.detailsLoaded = true;
        return item;
    }
    // Avatar items' display names start with a slash ("/Ducktales Logo Tee").
    if (!h.displayName.empty())
        item.name = h.displayName[0] == '/' ? h.displayName.substr(1) : h.displayName;
    item.installedName = AvatarItemPatchedFileName(&header[0], kAvatarDefaultTitleId);
    item.installed = installedNames.count(Upper(item.installedName)) != 0;
    return item;
}

void LoadItemDetails(Item* item, const std::vector<u64>& licensees, const std::vector<Drive>& drives)
{
    if (item->detailsLoaded)
        return;
    item->detailsLoaded = true;

    // Items are small (tens of KB), and the preview, icon.png, is inside the package.
    std::vector<u8> data;
    if (!ReadWholeFile(FullPath(item->path), &data) || data.empty())
    {
        item->error = "cannot read the file";
        return;
    }
    if (!AvatarItemValidate(&data[0], (u32)data.size(), &item->error))
        return;

    // icon.png is 128 x 128; the header's thumbnail is a 64 x 64 stand-in.
    StfsPackage pkg;
    std::vector<u8> png;
    const StfsFileEntry* icon = NULL;
    if (pkg.Open(&data[0], (u32)data.size(), NULL) && (icon = pkg.FindFile("icon.png")) != NULL &&
        pkg.ReadFile(*icon, &png, NULL) && !png.empty())
        DecodeIcon(&png[0], (u32)png.size(), &item->icon);
    const u8* thumbnail = NULL;
    u32 thumbnailBytes = 0;
    if (item->icon.empty() && StfsThumbnail(&data[0], (u32)data.size(), &thumbnail, &thumbnailBytes))
        DecodeIcon(thumbnail, thumbnailBytes, &item->icon);

    if (item->installed)
        for (size_t i = 0; i < drives.size(); i++)
            if (InstalledFor(InstalledFolder(drives[i]) + item->installedName, licensees))
                item->installedOn.push_back(drives[i].name);
    item->installed = !item->installedOn.empty();
}

ItemListing ScanItemFolder(const std::string& relative, const std::vector<Drive>& drives)
{
    ItemListing listing;
    std::string full = FullPath(relative);

    // One listing of each Content folder instead of lookups per item.
    std::set<std::string> installedNames;
    for (size_t d = 0; d < drives.size(); d++)
    {
        std::vector<std::string> installed = ListFolder(InstalledFolder(drives[d]), false);
        for (size_t i = 0; i < installed.size(); i++)
            installedNames.insert(Upper(installed[i]));
    }

    std::vector<std::string> files = ListFolder(full, false);
    listing.items.reserve(files.size());
    for (size_t i = 0; i < files.size(); i++)
        listing.items.push_back(ReadItemHeader(relative, files[i], installedNames));
    std::sort(listing.items.begin(), listing.items.end(), ItemLess);

    listing.folders = ListFolder(full, true);
    std::sort(listing.folders.begin(), listing.folders.end(), LessIgnoringCase);
    return listing;
}

static void IndexFolders(const std::string& relative, std::vector<std::string>* index)
{
    std::vector<std::string> folders = ListFolder(FullPath(relative), true);
    for (size_t i = 0; i < folders.size(); i++)
    {
        std::string path = Join(relative, folders[i]);
        index->push_back(path);
        IndexFolders(path, index);
    }
}

std::vector<std::string> IndexItemFolders()
{
    std::vector<std::string> index;
    IndexFolders("", &index);
    return index;
}

std::vector<std::string> SearchFolders(const std::vector<std::string>& index, const std::string& query)
{
    std::vector<std::string> words;
    std::string lowerQuery = Lower(query);
    size_t start = 0;
    while (start < lowerQuery.size())
    {
        size_t end = lowerQuery.find(' ', start);
        if (end == std::string::npos)
            end = lowerQuery.size();
        if (end > start)
            words.push_back(lowerQuery.substr(start, end - start));
        start = end + 1;
    }

    std::vector<std::string> matches;
    if (words.empty())
        return matches;
    for (size_t i = 0; i < index.size(); i++)
    {
        std::string name = Lower(FolderName(index[i]));
        bool all = true;
        for (size_t w = 0; w < words.size() && all; w++)
            all = name.find(words[w]) != std::string::npos;
        if (all)
            matches.push_back(index[i]);
    }
    std::sort(matches.begin(), matches.end(), FolderPathLess);
    return matches;
}

bool InstallItemFile(const std::string& path, bool removeSource, const std::vector<u64>& licensees,
                     const Drive& drive, std::string* error)
{
    std::string source = FullPath(path);
    std::vector<u8> data;
    if (!ReadWholeFile(source, &data) || data.empty())
    {
        *error = "cannot read the file";
        return false;
    }
    if (!AvatarItemValidate(&data[0], (u32)data.size(), error))
        return false;

    // XUIDs an installed copy already has (a profile that isn't around now) stay.
    AvatarPatchOptions options = PatchOptions(licensees);
    std::vector<u64> others =
        InstalledLicensees(InstalledFolder(drive) + AvatarItemPatchedFileName(&data[0], kAvatarDefaultTitleId));
    for (size_t i = 0; i < others.size(); i++)
        AddUnique(&options.extraXuids, others[i]);
    if (!AvatarItemPatch(&data[0], (u32)data.size(), options, error))
        return false;

    std::string target = drive.root + "\\" + ContentFolder() + "\\" + AvatarItemFileName(&data[0]);
    if (!MakeDirs(drive.root, ContentFolder()) || !WriteWholeFile(target, &data[0], (u32)data.size()))
    {
        *error = "cannot write to the Content folder on " + drive.name;
        return false;
    }
    if (removeSource && !DeleteFile(source))
    {
        *error = "installed, but the file could not be deleted";
        return false;
    }
    return true;
}

static void AddFailure(InstallAllResult* result, const std::string& name, const std::string& error)
{
    result->failed++;
    if (result->errors.size() < kMaxReportedErrors)
        result->errors.push_back(name + ": " + error);
}

static void InstallAllIn(const std::string& relative, bool removeSource, const std::vector<u64>& licensees,
                         const Drive& drive, InstallAllResult* result)
{
    std::string full = FullPath(relative);
    std::vector<std::string> files = ListFolder(full, false);
    for (size_t i = 0; i < files.size(); i++)
    {
        std::string error;
        if (InstallItemFile(Join(relative, files[i]), removeSource, licensees, drive, &error))
            result->installed++;
        else
            AddFailure(result, files[i], error);
    }
    std::vector<std::string> folders = ListFolder(full, true);
    for (size_t i = 0; i < folders.size(); i++)
        InstallAllIn(Join(relative, folders[i]), removeSource, licensees, drive, result);
}

InstallAllResult InstallAllItems(const std::string& relative, bool removeSource, const std::vector<u64>& licensees,
                                 const Drive& drive)
{
    InstallAllResult result;
    result.installed = 0;
    result.failed = 0;
    InstallAllIn(relative, removeSource, licensees, drive, &result);
    return result;
}

InstallAllResult UpdateInstalledItems(const std::vector<u64>& licensees, const std::vector<Drive>& drives)
{
    InstallAllResult result;
    result.installed = 0;
    result.failed = 0;
    for (size_t d = 0; d < drives.size(); d++)
    {
        std::string folder = InstalledFolder(drives[d]);
        std::vector<std::string> files = ListFolder(folder, false);
        for (size_t i = 0; i < files.size(); i++)
        {
            std::string path = folder + files[i];
            if (InstalledFor(path, licensees))
                continue;
            std::vector<u8> data;
            std::string error;
            // Entry [1] stays as it is; what's missing goes into free entries.
            AvatarPatchOptions options = PatchOptions(licensees);
            options.setXuid = false;
            if (!ReadWholeFile(path, &data) || data.empty())
                AddFailure(&result, files[i], "cannot read the file");
            else if (!AvatarItemPatch(&data[0], (u32)data.size(), options, &error))
                AddFailure(&result, files[i], error);
            else if (!WriteWholeFile(path, &data[0], (u32)data.size()))
                AddFailure(&result, files[i], "cannot write the file");
            else
                result.installed++;
        }
    }
    return result;
}

static void IndexSourcesIn(const std::string& relative, std::map<std::string, std::string>* index)
{
    std::string full = FullPath(relative);
    std::vector<std::string> files = ListFolder(full, false);
    for (size_t i = 0; i < files.size(); i++)
    {
        std::string path = Join(relative, files[i]);
        std::vector<u8> header;
        StfsHeader h;
        if (!ReadFilePrefix(FullPath(path), kItemHeaderBytes, &header) || header.size() < kAvatarOffsetAssetId + 16 ||
            !StfsParseHeader(&header[0], (u32)header.size(), &h, NULL) || h.contentType != kContentTypeAvatarItem)
            continue;
        index->insert(std::make_pair(AvatarItemFileName(&header[0]), path));
    }
    std::vector<std::string> folders = ListFolder(full, true);
    for (size_t i = 0; i < folders.size(); i++)
        IndexSourcesIn(Join(relative, folders[i]), index);
}

std::map<std::string, std::string> IndexItemSources()
{
    std::map<std::string, std::string> index;
    IndexSourcesIn("", &index);
    return index;
}

std::set<std::string> InstalledItemNames(const std::vector<Drive>& drives)
{
    std::set<std::string> names;
    for (size_t d = 0; d < drives.size(); d++)
    {
        std::vector<std::string> installed = ListFolder(InstalledFolder(drives[d]), false);
        for (size_t i = 0; i < installed.size(); i++)
            names.insert(Upper(installed[i]));
    }
    return names;
}
