// Files and drives on the console, through xsdk's POSIX layer. Paths are
// kernel paths: "game:\..." (the folder the app was launched from) or device
// paths such as "\Device\Harddisk0\Partition1\...", backslash separated.
#pragma once

#include "../core/types.h"

#include <string>
#include <vector>

struct Drive
{
    std::string name;   // "HDD", "USB 1", ...
    std::string root;   // device path without a trailing backslash
};

// The hard disk's data partition.
const Drive& HddDrive();

// The hard disk and every USB storage device that can be opened.
std::vector<Drive> AvailableDrives();

bool PathExists(const std::string& path);
bool ReadWholeFile(const std::string& path, std::vector<u8>* out);
// Reads at most `maxBytes` from the start.
bool ReadFilePrefix(const std::string& path, u32 maxBytes, std::vector<u8>* out);
// Writes the file, replacing it, then reads it back and compares.
bool WriteWholeFile(const std::string& path, const u8* data, u32 size);
bool DeleteFile(const std::string& path);
// Creates every missing folder of `relative` ("Content\\0000...\\AAAAAAAA")
// below `root` (a drive root, or "game:").
bool MakeDirs(const std::string& root, const std::string& relative);

// Names in a folder (no "." or ".."), files or folders only.
std::vector<std::string> ListFolder(const std::string& path, bool folders);
