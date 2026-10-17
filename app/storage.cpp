#include "storage.h"

#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const Drive kHdd = { "HDD", "\\Device\\Harddisk0\\Partition1" };

// USB storage shows up as Mass0..Mass2 (the dashboard's own numbering).
static const Drive kUsb[] =
{
    { "USB 1", "\\Device\\Mass0" },
    { "USB 2", "\\Device\\Mass1" },
    { "USB 3", "\\Device\\Mass2" },
};

const Drive& HddDrive()
{
    return kHdd;
}

std::vector<Drive> AvailableDrives()
{
    std::vector<Drive> drives;
    drives.push_back(kHdd);
    for (size_t i = 0; i < sizeof kUsb / sizeof kUsb[0]; i++)
    {
        DIR* dir = opendir((kUsb[i].root + "\\").c_str());
        if (dir)
        {
            closedir(dir);
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

std::vector<std::string> ListFolder(const std::string& path, bool folders)
{
    std::vector<std::string> names;
    DIR* dir = opendir(path.c_str());
    if (!dir)
        return names;
    // xsdk's readdir fills d_type from the directory listing, so no stat per entry.
    while (struct dirent* entry = readdir(dir))
    {
        std::string name = entry->d_name;
        if (name == "." || name == ".." || (entry->d_type == DT_DIR) != folders)
            continue;
        names.push_back(name);
    }
    closedir(dir);
    return names;
}
