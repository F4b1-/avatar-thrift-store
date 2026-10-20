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

// The drive the app was started from: a USB stick, or else the hard disk's
// data partition.
const Drive& AppDrive();

// The hard disk, every USB storage device with a Content folder and the one the
// app runs from: where profiles are looked for and items can be installed.
std::vector<Drive> AvailableDrives();

bool PathExists(const std::string& path);
bool ReadWholeFile(const std::string& path, std::vector<u8>* out);
// Reads at most `maxBytes` from the start.
bool ReadFilePrefix(const std::string& path, u32 maxBytes, std::vector<u8>* out);
// Writes the file, replacing it, then reads it back and compares.
bool WriteWholeFile(const std::string& path, const u8* data, u32 size);
// Creates every missing folder of `relative` ("Content\\0000...\\AAAAAAAA")
// below `root` (a drive root, or "game:").
bool MakeDirs(const std::string& root, const std::string& relative);

// A path for showing: "game:\Backups\x" -> "Backups\x", as a user finds it next
// to the app (game: means nothing outside it). Other paths stay as they are.
std::string ShownPath(const std::string& path);

// Names in a folder (no "." or ".."), files or folders only.
std::vector<std::string> ListFolder(const std::string& path, bool folders);
