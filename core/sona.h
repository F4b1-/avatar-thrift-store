// Sona codes: avatars moving between the console and 360sona
// (https://360sona.com), a web avatar editor that reads and writes the same
// avatar description the console keeps in every profile.
//
// The avatar description is 1000 bytes, big-endian, version 0. The dashboard
// stores it as profile setting 0x63E80044 (Avatar Information) in the
// profile's dashboard GPD, FFFE07D1.gpd. Its item GUIDs are stored in the same
// byte order as an avatar item's asset id (package offset 0x3E1), so the last
// 4 bytes of each one are the item's title id.
//
// A sona code is that description as text, so it survives a Discord message, a
// forum post or a text file:
//
//   360sona avatar v1
//   # Paste this whole thing into 360sona -> My Sonas -> Import.
//   <base64 of the 1000 bytes, wrapped at 76 characters>
//
// Readers ignore the header and comment lines, quoting ("> "), wrapping and
// whitespace, and accept base64url and missing padding, so a code that went
// through a mail client still reads.
#pragma once

#include "types.h"

#include <string>
#include <vector>

enum
{
    kSonaManifestSize = 1000,
    kSonaGuidSize     = 16,
    kSonaOffsetFigure = 288,    // the body; zero in a description without one
    kSonaOffsetOwner  = 901,    // u64, the XUID of the profile it was saved for
};

// The dashboard GPD's avatar setting, and that GPD's file name in a profile.
static const u32   kSonaSettingId    = 0x63E80044;
static const char* const kSonaDashboardGpd = "FFFE07D1.gpd";

extern const char* const kSonaCodeHeader;   // "360sona avatar v1"

// Checks the size, the version and that there is a body.
bool SonaValidate(const u8* manifest, u32 size, std::string* error);

// The text form of a description. `manifest` is kSonaManifestSize bytes.
std::string SonaEncode(const u8* manifest);

// Reads a sona code (or bare base64) and checks the result with SonaValidate.
bool SonaDecode(const std::string& text, std::vector<u8>* manifest, std::string* error);

// Offsets of the 28 item GUIDs in a description: head shape (3), face (6),
// body (2), worn items (13) and the four the editor keeps for required slots.
const std::vector<u32>& SonaGuidOffsets();

// The distinct non-zero item GUIDs a description refers to, in slot order.
std::vector<std::vector<u8> > SonaItemIds(const u8* manifest);

// Built-in items (every console has them, nothing to unlock) share one
// suffix: the GUID's last 8 bytes are C1C8F109A19CB2E0.
bool SonaIsBuiltIn(const u8* guid);

// Rewrites every slot holding `from` to `to`. Returns how many changed.
int SonaReplaceId(u8* manifest, const u8* from, const u8* to);

// 32 uppercase hex digits, the form item package files are named after.
std::string SonaIdHex(const u8* guid);
