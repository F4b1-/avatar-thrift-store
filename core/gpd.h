// GPD ("XDBF") files: per-title profile databases (achievements, settings,
// titles, ...). Avatar award records (namespace 6) live only in the title GPDs
// inside the profile's PEC (profile embedded content).
//
// Layout (big-endian):
//   0x00 "XDBF", version, entry table capacity, entry count,
//        free table capacity, free count                        (0x18 bytes)
//   entry table: capacity x { u16 namespace, u64 id, u32 offset, u32 length }
//   free table:  capacity x { u32 offset, u32 length }
//   data area: offsets are relative to its start
#pragma once

#include "types.h"

#include <string>
#include <vector>

enum GpdNamespace
{
    kGpdAchievement = 1,
    kGpdImage       = 2,
    kGpdSetting     = 3,
    kGpdTitle       = 4,
    kGpdString      = 5,
    kGpdAvatarAward = 6,
};

// Entry ids 1 and 2 of every namespace are sync bookkeeping, not records.
static const u64 kGpdSyncListId = 1;
static const u64 kGpdSyncDataId = 2;

struct GpdEntry
{
    u16 ns;
    u64 id;
    u32 offset;   // relative to the data area
    u32 length;
};

struct GpdFile
{
    u32                   version;
    u32                   entryCapacity;
    u32                   freeCapacity;
    u32                   dataStart;
    std::vector<GpdEntry> entries;
    std::vector<u8>       raw;

    const u8* EntryData(const GpdEntry& e) const { return &raw[dataStart + e.offset]; }
};

bool GpdParse(const u8* data, u32 size, GpdFile* out, std::string* error);

// Avatar award record (namespace 6). Entry id = (title id << 32) |
// (award index << 16) | (body type << 8): body type 1 male, 2 female, 3 both.
struct GpdAvatarAward
{
    u8          assetId[16];    // same as the asset package's id/file name
    u32         imageId;
    u32         flags;          // achievement flags; 0x20000 = earned
    u64         unlockTime;     // FILETIME
    u32         subcategory;
    u32         unknown;
    std::string name;           // UTF-8
    std::string unlockedDescription;
    std::string lockedDescription;
};

enum
{
    kGpdFlagEarnedOnline = 0x00010000,
    kGpdFlagEarned       = 0x00020000,
};

bool GpdParseAvatarAward(const u8* data, u32 size, GpdAvatarAward* out);
