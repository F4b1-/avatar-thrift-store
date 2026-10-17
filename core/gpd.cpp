#include "gpd.h"
#include "bytes.h"

#include <string.h>

bool GpdParse(const u8* data, u32 size, GpdFile* out, std::string* error)
{
    if (size < 0x18 || memcmp(data, "XDBF", 4) != 0)
    {
        if (error) *error = "not a GPD file";
        return false;
    }

    out->version       = ReadBE32(data + 0x04);
    out->entryCapacity = ReadBE32(data + 0x08);
    u32 count          = ReadBE32(data + 0x0C);
    out->freeCapacity  = ReadBE32(data + 0x10);
    out->dataStart     = 0x18 + out->entryCapacity * 0x12 + out->freeCapacity * 8;

    if (count > out->entryCapacity || out->dataStart > size)
    {
        if (error) *error = "corrupt GPD header";
        return false;
    }

    out->raw.assign(data, data + size);
    out->entries.clear();
    for (u32 i = 0; i < count; i++)
    {
        const u8* e = data + 0x18 + i * 0x12;
        GpdEntry entry;
        entry.ns     = ReadBE16(e);
        entry.id     = ReadBE64(e + 2);
        entry.offset = ReadBE32(e + 10);
        entry.length = ReadBE32(e + 14);
        if ((u64)out->dataStart + entry.offset + entry.length > size)
        {
            if (error) *error = "GPD entry outside the file";
            return false;
        }
        out->entries.push_back(entry);
    }
    return true;
}

// Reads a zero-terminated UTF-16BE string (ASCII subset kept, others '?').
static std::string ReadWide(const u8* p, u32 size, u32* pos)
{
    std::string out;
    while (*pos + 1 < size)
    {
        u16 c = ReadBE16(p + *pos);
        *pos += 2;
        if (c == 0)
            break;
        out += c < 0x80 ? (char)c : '?';
    }
    return out;
}

bool GpdParseAvatarAward(const u8* data, u32 size, GpdAvatarAward* out)
{
    if (size < 0x2C || ReadBE32(data) != 0x2C)
        return false;

    memcpy(out->assetId, data + 0x04, 16);
    out->imageId     = ReadBE32(data + 0x14);
    out->flags       = ReadBE32(data + 0x18);
    out->unlockTime  = ReadBE64(data + 0x1C);
    out->subcategory = ReadBE32(data + 0x24);
    out->unknown     = ReadBE32(data + 0x28);

    u32 pos = 0x2C;
    out->name                = ReadWide(data, size, &pos);
    out->unlockedDescription = ReadWide(data, size, &pos);
    out->lockedDescription   = ReadWide(data, size, &pos);
    return true;
}
