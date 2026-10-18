#include "storage.h"

#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xsdk/kernel.h"

static const Drive kHdd = { "HDD", "\\Device\\Harddisk0\\Partition1" };

// USB storage shows up as Mass0..Mass2 (the dashboard's own numbering).
static const Drive kUsb[] =
{
    { "USB 1", "\\Device\\Mass0" },
    { "USB 2", "\\Device\\Mass1" },
    { "USB 3", "\\Device\\Mass2" },
};

// Folders are opened and listed through the kernel, not xsdk's opendir/readdir:
// on a USB stick, a readdir that returns an entry crashes the console about a
// second later (the hard disk is fine).
static bool OpenFolder(const std::string& path, HANDLE* handle)
{
    std::string folder = path;
    while (folder.size() > 1 && folder[folder.size() - 1] == '\\')
        folder.erase(folder.size() - 1);
    ANSI_STRING name = { (uint16_t)folder.size(), (uint16_t)(folder.size() + 1), folder.c_str() };
    // Drive paths ("game:\...") are looked up among the DOS devices.
    OBJECT_ATTRIBUTES attributes = { folder[0] == '\\' ? (HANDLE)0 : OB_DOS_DEVICES, &name, 0 };
    IO_STATUS_BLOCK io;
    return NT_SUCCESS(NtCreateFile(handle, GENERIC_READ | SYNCHRONIZE, &attributes, &io, NULL, FILE_ATTRIBUTE_NORMAL,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
                                   FILE_SYNCHRONOUS_IO_NONALERT | FILE_DIRECTORY_FILE));
}

const Drive& AppDrive()
{
    // The loader's path of the running xex: "\Device\Mass0\AvatarThriftStore\...".
    const char* image = ExLoadedImageName ? ExLoadedImageName : "";
    for (size_t i = 0; i < sizeof kUsb / sizeof kUsb[0]; i++)
    {
        const std::string& root = kUsb[i].root;
        if (strncasecmp(image, root.c_str(), root.size()) == 0 && image[root.size()] == '\\')
            return kUsb[i];
    }
    return kHdd;
}

std::vector<Drive> AvailableDrives()
{
    std::vector<Drive> drives;
    drives.push_back(kHdd);
    // Probed by their Content folder, where profiles are: the device itself,
    // "\Device\Mass0", can't be opened as a folder (0xC0000103).
    // The stick the app runs from counts even without one.
    for (size_t i = 0; i < sizeof kUsb / sizeof kUsb[0]; i++)
    {
        HANDLE handle;
        if (&kUsb[i] == &AppDrive())
            drives.push_back(kUsb[i]);
        else if (OpenFolder(kUsb[i].root + "\\Content", &handle))
        {
            NtClose(handle);
            drives.push_back(kUsb[i]);
        }
    }
    return drives;
}

bool PathExists(const std::string& path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

bool ReadFilePrefix(const std::string& path, u32 maxBytes, std::vector<u8>* out)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    struct stat info;
    if (fstat(fd, &info) != 0)
    {
        close(fd);
        return false;
    }
    u32 size = (u32)info.st_size < maxBytes ? (u32)info.st_size : maxBytes;
    out->resize(size);
    u32 done = 0;
    while (done < size)
    {
        ssize_t n = read(fd, &(*out)[done], size - done);
        if (n <= 0)
            break;
        done += (u32)n;
    }
    close(fd);
    out->resize(done);
    return done == size;
}

bool ReadWholeFile(const std::string& path, std::vector<u8>* out)
{
    return ReadFilePrefix(path, 0xFFFFFFFFu, out);
}

bool WriteWholeFile(const std::string& path, const u8* data, u32 size)
{
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    u32 done = 0;
    while (done < size)
    {
        ssize_t n = write(fd, data + done, size - done);
        if (n <= 0)
            break;
        done += (u32)n;
    }
    bool closed = close(fd) == 0;
    if (done != size || !closed)
        return false;

    std::vector<u8> back;
    return ReadWholeFile(path, &back) && back.size() == size && (size == 0 || memcmp(&back[0], data, size) == 0);
}

bool DeleteFile(const std::string& path)
{
    return unlink(path.c_str()) == 0;
}

bool MakeDirs(const std::string& root, const std::string& relative)
{
    std::string path = root;
    size_t start = 0;
    while (start < relative.size())
    {
        size_t end = relative.find('\\', start);
        if (end == std::string::npos)
            end = relative.size();
        path += "\\" + relative.substr(start, end - start);
        if (!PathExists(path) && mkdir(path.c_str(), 0777) != 0)
            return false;
        start = end + 1;
    }
    return true;
}

// Many entries per query, into a buffer in the image's own memory.
static u64 g_listing[64 * 1024 / 8];

std::vector<std::string> ListFolder(const std::string& path, bool folders)
{
    std::vector<std::string> names;
    HANDLE handle;
    if (!OpenFolder(path, &handle))
        return names;
    // The pattern is read only when a scan starts.
    static const ANSI_STRING kAll = { 1, 2, "*" };
    for (bool first = true;; first = false)
    {
        IO_STATUS_BLOCK io;
        NTSTATUS status = NtQueryDirectoryFile(handle, 0, NULL, NULL, &io, (FILE_DIRECTORY_INFORMATION*)g_listing,
                                               sizeof g_listing, first ? &kAll : NULL, first ? 1 : 0);
        // Also the end: STATUS_NO_MORE_FILES, or STATUS_NO_SUCH_FILE for an empty folder.
        if (!NT_SUCCESS(status))
            break;
        for (u32 offset = 0;;)
        {
            const FILE_DIRECTORY_INFORMATION* entry = (const FILE_DIRECTORY_INFORMATION*)((const u8*)g_listing + offset);
            if (offset + sizeof *entry + entry->file_name_length > sizeof g_listing)
                break;
            std::string name(entry->file_name, entry->file_name_length);
            bool isFolder = (entry->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (name != "." && name != ".." && isFolder == folders)
                names.push_back(name);
            if (!entry->next_entry_offset)
                break;
            offset += entry->next_entry_offset;
        }
    }
    NtClose(handle);
    return names;
}
