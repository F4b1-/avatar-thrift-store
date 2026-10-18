#include "sona.h"
#include "bytes.h"

#include <string.h>

const char* const kSonaCodeHeader = "360sona avatar v1";

static const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static bool AllZero(const u8* p, u32 n)
{
    for (u32 i = 0; i < n; i++)
        if (p[i])
            return false;
    return true;
}

bool SonaValidate(const u8* manifest, u32 size, std::string* error)
{
    if (size != kSonaManifestSize)
    {
        if (error) *error = "an avatar description is 1000 bytes";
        return false;
    }
    if (ReadBE32(manifest) != 0)
    {
        if (error) *error = "unknown avatar description version";
        return false;
    }
    if (AllZero(manifest + kSonaOffsetFigure, kSonaGuidSize))
    {
        if (error) *error = "the avatar has no body";
        return false;
    }
    return true;
}

std::string SonaEncode(const u8* manifest)
{
    std::string b64;
    for (u32 i = 0; i < kSonaManifestSize; i += 3)
    {
        u32 n = kSonaManifestSize - i < 3 ? kSonaManifestSize - i : 3;
        u32 v = (u32)manifest[i] << 16 | (n > 1 ? (u32)manifest[i + 1] << 8 : 0) | (n > 2 ? manifest[i + 2] : 0);
        b64 += kBase64[v >> 18 & 63];
        b64 += kBase64[v >> 12 & 63];
        b64 += n > 1 ? kBase64[v >> 6 & 63] : '=';
        b64 += n > 2 ? kBase64[v & 63] : '=';
    }
    std::string out = std::string(kSonaCodeHeader) + "\n# Paste this whole thing into 360sona -> My Sonas -> Import.\n";
    for (size_t i = 0; i < b64.size(); i += 76)
        out += b64.substr(i, 76) + "\n";
    return out;
}

static int Base64Value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

bool SonaDecode(const std::string& text, std::vector<u8>* manifest, std::string* error)
{
    // Keep only payload lines: not the header, not a comment, not prose (a
    // line with a space inside it, such as a name typed above the code).
    std::string body;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;

        size_t a = 0;
        while (a < line.size() && (line[a] == '>' || IsSpace(line[a])))
            a++;
        size_t b = line.size();
        while (b > a && IsSpace(line[b - 1]))
            b--;
        line = line.substr(a, b - a);
        if (line.empty() || line == kSonaCodeHeader || line[0] == '#' || line.compare(0, 2, "//") == 0)
            continue;
        bool prose = false;
        for (size_t i = 0; i < line.size() && !prose; i++)
            prose = IsSpace(line[i]);
        if (!prose)
            body += line;
    }

    manifest->clear();
    u32 acc = 0;
    int bits = 0;
    for (size_t i = 0; i < body.size(); i++)
    {
        if (body[i] == '=')
            break;
        int v = Base64Value(body[i]);
        if (v < 0)
            continue;
        acc = acc << 6 | (u32)v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            manifest->push_back((u8)(acc >> bits));
        }
    }
    if (manifest->empty())
    {
        if (error) *error = "no sona code in the text";
        return false;
    }
    return SonaValidate(&(*manifest)[0], (u32)manifest->size(), error);
}

const std::vector<u32>& SonaGuidOffsets()
{
    static std::vector<u32> offsets;
    if (offsets.empty())
    {
        for (u32 i = 0; i < 3; i++)  offsets.push_back(12 + i * 16);
        for (u32 i = 0; i < 6; i++)  offsets.push_back(60 + i * 32);
        offsets.push_back(288);
        offsets.push_back(320);
        for (u32 i = 0; i < 13; i++) offsets.push_back(352 + i * 32);
        for (u32 i = 0; i < 4; i++)  offsets.push_back(768 + i * 32);
    }
    return offsets;
}

std::vector<std::vector<u8> > SonaItemIds(const u8* manifest)
{
    std::vector<std::vector<u8> > ids;
    const std::vector<u32>& offsets = SonaGuidOffsets();
    for (size_t i = 0; i < offsets.size(); i++)
    {
        const u8* g = manifest + offsets[i];
        if (AllZero(g, kSonaGuidSize))
            continue;
        std::vector<u8> id(g, g + kSonaGuidSize);
        bool seen = false;
        for (size_t j = 0; j < ids.size() && !seen; j++)
            seen = ids[j] == id;
        if (!seen)
            ids.push_back(id);
    }
    return ids;
}

bool SonaIsBuiltIn(const u8* guid)
{
    static const u8 kSuffix[8] = { 0xC1, 0xC8, 0xF1, 0x09, 0xA1, 0x9C, 0xB2, 0xE0 };
    return memcmp(guid + 8, kSuffix, 8) == 0;
}

int SonaReplaceId(u8* manifest, const u8* from, const u8* to)
{
    int changed = 0;
    const std::vector<u32>& offsets = SonaGuidOffsets();
    for (size_t i = 0; i < offsets.size(); i++)
    {
        u8* g = manifest + offsets[i];
        if (memcmp(g, from, kSonaGuidSize) == 0 && memcmp(g, to, kSonaGuidSize) != 0)
        {
            memcpy(g, to, kSonaGuidSize);
            changed++;
        }
    }
    return changed;
}

std::string SonaIdHex(const u8* guid)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (u32 i = 0; i < kSonaGuidSize; i++)
    {
        out += kHex[guid[i] >> 4];
        out += kHex[guid[i] & 15];
    }
    return out;
}
