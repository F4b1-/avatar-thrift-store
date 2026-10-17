#include "stfs_package.h"
#include "bytes.h"
#include "sha1.h"
#include "stfs.h"

#include <ctype.h>
#include <string.h>

enum
{
    kBlockSize          = 0x1000,
    kHashEntrySize      = 0x18,
    kBlocksPerTable     = 0xAA,
    kBlocksPerL1        = 0x70E4,
    kFileEntrySize      = 0x40,

    // Volume descriptor location and field offsets relative to it.
    kPackageVolume      = 0x379,
    kPecVolume          = 0x244,
    kPecBase            = 0x1000,
    kVolBlockSeparation = 0x02,
    kVolFileTableCount  = 0x03,     // LE16
    kVolFileTableBlock  = 0x05,     // LE24
    kVolTopHash         = 0x08,     // SHA-1 of the top-level hash table
    kVolAllocatedBlocks = 0x1C,     // BE32
};

// Index of `block`'s entry in the hash table of `level` that covers it.
static u32 HashEntryIndex(u32 level, u32 block)
{
    if (level == 0)
        return block % kBlocksPerTable;
    if (level == 1)
        return (block / kBlocksPerTable) % kBlocksPerTable;
    return block / kBlocksPerL1;
}

StfsPackage::StfsPackage()
    : m_data(NULL), m_size(0), m_volume(0), m_base(0), m_sex(0), m_topLevel(0), m_topTable(0), m_allocatedBlocks(0)
{
    m_blockStep[0] = m_blockStep[1] = 0;
}

// Number of hash table blocks (of either level) that precede `level`'s table
// covering `block`, counted in blocks from m_base.
u32 StfsPackage::HashTableOffset(u32 level, u32 block) const
{
    u32 num = 0;
    if (level == 0)
    {
        if (block >= kBlocksPerTable)
        {
            num = (block / kBlocksPerTable) * m_blockStep[0];
            num += ((block / kBlocksPerL1) + 1) << m_sex;
            if (block / kBlocksPerL1 != 0)
                num += 1 << m_sex;
        }
    }
    else if (level == 1)
    {
        if (block < kBlocksPerL1)
            num = m_blockStep[0];
        else
            num = (1 << m_sex) + m_blockStep[1] * (block / kBlocksPerL1);
    }
    else
    {
        num = m_blockStep[1];
    }
    return m_base + num * kBlockSize;
}

u32 StfsPackage::BlockToOffset(u32 block) const
{
    u32 n = (((block + kBlocksPerTable) / kBlocksPerTable) << m_sex) + block;
    if (block >= kBlocksPerTable)
    {
        n += ((block + kBlocksPerL1) / kBlocksPerL1) << m_sex;
        if (block >= kBlocksPerL1)
            n += 1 << m_sex;
    }
    return m_base + n * kBlockSize;
}

bool StfsPackage::HashEntryOffset(u32 block, u32* offset) const
{
    if (block >= m_allocatedBlocks)
        return false;

    // Walk down from the top table; for male packages the status byte of the
    // parent entry (bit 0x40) selects which copy of the child table is active.
    u32 table = m_topTable;
    for (u32 level = m_topLevel; level > 0; level--)
    {
        u32 index = (level == 2) ? block / kBlocksPerL1 : (block / kBlocksPerTable) % kBlocksPerTable;
        u32 entry = table + index * kHashEntrySize;
        if (entry + kHashEntrySize > m_size)
            return false;
        u32 child = HashTableOffset(level - 1, block);
        if (m_sex && (m_data[entry + 20] & 0x40))
            child += kBlockSize;
        table = child;
    }

    *offset = table + (block % kBlocksPerTable) * kHashEntrySize;
    return *offset + kHashEntrySize <= m_size;
}

bool StfsPackage::NextBlock(u32 block, u32* next) const
{
    u32 entry;
    if (!HashEntryOffset(block, &entry))
        return false;
    *next = ReadBE24(m_data + entry + 21);
    return true;
}

bool StfsPackage::BlockChain(u32 start, u32 count, bool consecutive, std::vector<u32>* chain) const
{
    chain->clear();
    u32 block = start;
    for (u32 i = 0; i < count; i++)
    {
        if (block >= m_allocatedBlocks || BlockToOffset(block) + kBlockSize > m_size)
            return false;
        chain->push_back(block);
        if (i + 1 == count)
            break;
        if (consecutive)
            block++;
        else if (!NextBlock(block, &block))
            return false;
    }
    return true;
}

bool StfsPackage::Open(const u8* data, u32 size, std::string* error)
{
    StfsHeader header;
    if (!StfsParseHeader(data, size, &header, error))
        return false;
    return OpenVolume(data, size, kPackageVolume, StfsHashedHeaderEnd(header.headerSize), error);
}

bool StfsPackage::OpenPec(const u8* data, u32 size, std::string* error)
{
    if (size < kPecBase || data[kPecVolume] != 0x24)
    {
        if (error) *error = "not a PEC file";
        return false;
    }
    return OpenVolume(data, size, kPecVolume, kPecBase, error);
}

bool StfsPackage::OpenVolume(const u8* data, u32 size, u32 volumeDescriptor, u32 base, std::string* error)
{
    const u8* vol = data + volumeDescriptor;
    m_data = data;
    m_size = size;
    m_volume = volumeDescriptor;
    m_base = base;
    m_sex = (~vol[kVolBlockSeparation]) & 1;
    m_blockStep[0] = m_sex ? 0xAC : 0xAB;
    m_blockStep[1] = m_sex ? 0x723A : 0x718F;
    m_allocatedBlocks = ReadBE32(vol + kVolAllocatedBlocks);
    m_topLevel = m_allocatedBlocks <= kBlocksPerTable ? 0 : (m_allocatedBlocks <= kBlocksPerL1 ? 1 : 2);
    m_topTable = HashTableOffset(m_topLevel, 0);
    if (m_sex && (vol[kVolBlockSeparation] & 2))
        m_topTable += kBlockSize;

    u32 tableBlocks = ReadLE16(vol + kVolFileTableCount);
    u32 tableStart = ReadLE24(vol + kVolFileTableBlock);

    std::vector<u32> chain;
    if (!BlockChain(tableStart, tableBlocks, false, &chain))
    {
        if (error) *error = "file table points outside the package";
        return false;
    }

    m_files.clear();
    for (size_t b = 0; b < chain.size(); b++)
    {
        const u8* block = data + BlockToOffset(chain[b]);
        for (u32 i = 0; i < kBlockSize / kFileEntrySize; i++)
        {
            const u8* e = block + i * kFileEntrySize;
            u32 nameLength = e[0x28] & 0x3F;
            if (nameLength == 0)
                continue;

            StfsFileEntry f;
            f.name.assign((const char*)e, nameLength);
            f.consecutive = (e[0x28] & 0x40) != 0;
            f.isDirectory = (e[0x28] & 0x80) != 0;
            f.blockCount  = ReadLE24(e + 0x29);
            f.startBlock  = ReadLE24(e + 0x2F);
            f.parentIndex = ReadBE16(e + 0x32);
            f.size        = ReadBE32(e + 0x34);
            m_files.push_back(f);
        }
    }
    return true;
}

const StfsFileEntry* StfsPackage::FindFile(const char* name) const
{
    for (size_t i = 0; i < m_files.size(); i++)
    {
        const StfsFileEntry& f = m_files[i];
        if (f.isDirectory || f.parentIndex != 0xFFFF || f.name.size() != strlen(name))
            continue;
        bool same = true;
        for (size_t c = 0; c < f.name.size() && same; c++)
            same = tolower((unsigned char)f.name[c]) == tolower((unsigned char)name[c]);
        if (same)
            return &f;
    }
    return NULL;
}

bool StfsPackage::ReadFile(const StfsFileEntry& entry, std::vector<u8>* out, std::string* error) const
{
    u32 blocks = (entry.size + kBlockSize - 1) / kBlockSize;
    std::vector<u32> chain;
    if (!BlockChain(entry.startBlock, blocks, entry.consecutive, &chain))
    {
        if (error) *error = "broken block chain for " + entry.name;
        return false;
    }

    out->resize(entry.size);
    u32 remaining = entry.size;
    for (size_t i = 0; i < chain.size(); i++)
    {
        u32 n = remaining < (u32)kBlockSize ? remaining : (u32)kBlockSize;
        memcpy(&(*out)[i * kBlockSize], m_data + BlockToOffset(chain[i]), n);
        remaining -= n;
    }
    return true;
}

bool StfsPackage::ActiveTables(u32 block, u32 tables[3]) const
{
    if (block >= m_allocatedBlocks)
        return false;
    tables[m_topLevel] = m_topTable;
    for (u32 level = m_topLevel; level > 0; level--)
    {
        u32 entry = tables[level] + HashEntryIndex(level, block) * kHashEntrySize;
        if (entry + kHashEntrySize > m_size)
            return false;
        u32 child = HashTableOffset(level - 1, block);
        if (m_sex && (m_data[entry + 20] & 0x40))
            child += kBlockSize;
        tables[level - 1] = child;
    }
    for (u32 level = 0; level <= m_topLevel; level++)
        if (tables[level] + kBlockSize > m_size)
            return false;
    return true;
}

// Stores the block's new SHA-1 in its level-0 entry, then each changed table's
// SHA-1 in the level above, ending with the top hash in the volume descriptor.
bool StfsPackage::RehashBlock(u8* data, u32 block)
{
    u32 tables[3];
    if (!ActiveTables(block, tables))
        return false;

    Sha1(data + BlockToOffset(block), kBlockSize, data + tables[0] + HashEntryIndex(0, block) * kHashEntrySize);
    for (u32 level = 1; level <= m_topLevel; level++)
        Sha1(data + tables[level - 1], kBlockSize,
             data + tables[level] + HashEntryIndex(level, block) * kHashEntrySize);
    Sha1(data + tables[m_topLevel], kBlockSize, data + m_volume + kVolTopHash);
    return true;
}

bool StfsPackage::ReplaceFileData(u8* data, const StfsFileEntry& entry, const u8* content, u32 size,
                                  std::string* error)
{
    if (data != m_data || entry.isDirectory || size != entry.size)
    {
        if (error) *error = "can only replace a file with data of the same size";
        return false;
    }

    std::vector<u32> chain;
    if (!BlockChain(entry.startBlock, (size + kBlockSize - 1) / kBlockSize, entry.consecutive, &chain))
    {
        if (error) *error = "broken block chain for " + entry.name;
        return false;
    }

    // The bytes after the file's end in its last block stay as they were.
    u32 remaining = size;
    for (size_t i = 0; i < chain.size(); i++)
    {
        u32 n = remaining < (u32)kBlockSize ? remaining : (u32)kBlockSize;
        memcpy(data + BlockToOffset(chain[i]), content + i * kBlockSize, n);
        remaining -= n;
    }
    for (size_t i = 0; i < chain.size(); i++)
    {
        if (!RehashBlock(data, chain[i]))
        {
            if (error) *error = "hash table outside the package for " + entry.name;
            return false;
        }
    }
    return true;
}

u32 StfsPackage::VerifyHashTables(u32* checked) const
{
    u32 bad = 0;
    *checked = 0;
    u8 digest[20];

    // One walk per level-0 table: its hash and, once per level-1 table, that one's.
    for (u32 block = 0; block < m_allocatedBlocks; block += kBlocksPerTable)
    {
        u32 tables[3];
        (*checked)++;
        if (!ActiveTables(block, tables))
        {
            bad++;
            continue;
        }
        for (u32 level = 1; level <= m_topLevel; level++)
        {
            if (level == 2 && block % kBlocksPerL1 != 0)
                continue;
            Sha1(m_data + tables[level - 1], kBlockSize, digest);
            if (memcmp(digest, m_data + tables[level] + HashEntryIndex(level, block) * kHashEntrySize, 20) != 0)
                bad++;
        }
    }

    (*checked)++;
    if (m_topTable + kBlockSize > m_size)
        return bad + 1;
    Sha1(m_data + m_topTable, kBlockSize, digest);
    if (memcmp(digest, m_data + m_volume + kVolTopHash, 20) != 0)
        bad++;
    return bad;
}

u32 StfsPackage::VerifyAllBlocks(u32* checked) const
{
    u32 bad = 0;
    *checked = 0;

    std::vector<u32> blocks;
    const u8* vol = m_data + m_volume;
    BlockChain(ReadLE24(vol + kVolFileTableBlock), ReadLE16(vol + kVolFileTableCount), false, &blocks);
    for (size_t i = 0; i < m_files.size(); i++)
    {
        const StfsFileEntry& f = m_files[i];
        if (f.isDirectory || f.size == 0)
            continue;
        std::vector<u32> chain;
        if (!BlockChain(f.startBlock, (f.size + kBlockSize - 1) / kBlockSize, f.consecutive, &chain))
        {
            bad++;
            continue;
        }
        blocks.insert(blocks.end(), chain.begin(), chain.end());
    }

    for (size_t i = 0; i < blocks.size(); i++)
    {
        u32 entry;
        u8 digest[20];
        (*checked)++;
        if (!HashEntryOffset(blocks[i], &entry))
        {
            bad++;
            continue;
        }
        Sha1(m_data + BlockToOffset(blocks[i]), kBlockSize, digest);
        if (memcmp(digest, m_data + entry, 20) != 0)
            bad++;
    }
    return bad;
}
