// Access to the files inside an in-memory STFS package: reading, and
// same-size in-place replacement with rehashing.
//
// Block layout: data blocks are 0x1000 bytes, starting after the aligned
// header. Every 0xAA data blocks are preceded by a level-0 hash table, every
// 0x70E4 by a level-1 table. "Male" packages (read/write, e.g. CON profiles)
// keep two copies of each hash table and flag the active one; "female"
// packages (read-only, e.g. LIVE downloads) have one.
// Each hash entry: SHA-1 of the block (20), status (1), next block (BE24).
#pragma once

#include "types.h"

#include <string>
#include <vector>

struct StfsFileEntry
{
    std::string name;
    bool        isDirectory;
    bool        consecutive;    // blocks are contiguous, no chain lookup needed
    u32         blockCount;
    u32         startBlock;
    u16         parentIndex;    // 0xFFFF = root
    u32         size;
};

class StfsPackage
{
public:
    StfsPackage();

    // `data` must stay valid for the lifetime of the object.
    bool Open(const u8* data, u32 size, std::string* error);

    // Opens a PEC (profile embedded content): console certificate header,
    // volume descriptor at 0x244, first hash table at 0x1000.
    bool OpenPec(const u8* data, u32 size, std::string* error);

    const std::vector<StfsFileEntry>& Files() const { return m_files; }
    const StfsFileEntry* FindFile(const char* name) const;   // root-level, case-insensitive
    bool ReadFile(const StfsFileEntry& entry, std::vector<u8>* out, std::string* error) const;

    // Recomputes the SHA-1 of every block reachable from the file table and
    // compares it with the hash tables. Returns the number of mismatches.
    u32 VerifyAllBlocks(u32* checked) const;

    // Checks each active hash table against its entry one level up, and the
    // top table against the volume descriptor. Returns the number of mismatches.
    u32 VerifyHashTables(u32* checked) const;

    // Overwrites a file's contents in place with `size` == entry.size bytes,
    // then rehashes its blocks up through the hash tables to the volume
    // descriptor's top hash. `data` must be the (writable) buffer passed to
    // Open. The package header hash and signature are left to the caller.
    bool ReplaceFileData(u8* data, const StfsFileEntry& entry, const u8* content, u32 size,
                         std::string* error);

private:
    bool OpenVolume(const u8* data, u32 size, u32 volumeDescriptor, u32 base, std::string* error);
    u32  BlockToOffset(u32 block) const;
    // Offsets of the active hash table at each level (0..m_topLevel) covering `block`.
    bool ActiveTables(u32 block, u32 tables[3]) const;
    bool RehashBlock(u8* data, u32 block);
    bool HashEntryOffset(u32 block, u32* offset) const;
    bool NextBlock(u32 block, u32* next) const;
    bool BlockChain(u32 start, u32 count, bool consecutive, std::vector<u32>* chain) const;
    u32  HashTableOffset(u32 level, u32 block) const;

    const u8* m_data;
    u32       m_size;
    u32       m_volume;         // offset of the STFS volume descriptor
    u32       m_base;           // first hash table (= aligned header end)
    u32       m_sex;            // 0 = female (1 table copy), 1 = male (2 copies)
    u32       m_blockStep[2];
    u32       m_topLevel;
    u32       m_topTable;       // offset of the active top-level hash table
    u32       m_allocatedBlocks;
    std::vector<StfsFileEntry> m_files;
};
