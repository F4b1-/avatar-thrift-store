# Status (2026-10-17)

**Avatar items work on offline profiles and Xbox Live profiles.** Test 8b: an
offline test profile converted with `avtool set-xuid --live`, resigned in
Horizon, plus the Ducktales tee patched for the same XUID: the profile loads,
the tee shows up in the avatar editor and can be equipped. Items installed by
the app, licensed to the shared XUID and a Live profile's own online XUID, can
be selected by that Live profile too.

New items only need patching and copying; the profile stays as it is (the
license binds an item to the XUID, the profile keeps no item list).

## Working recipe

1. Profile: `avtool set-xuid --live --out <new> <profile>` gives it the shared
   online XUID `00090000AAAAAAAA` and makes it Live-flagged the way 360mpGui's
   "Offline 2 Online" maker does (see "Account file"). Then resign it (Horizon,
   or the app's own activation) and copy it back to
   `Content\<offline xuid>\FFFE07D1\00010000\`.
2. Items: `avtool patch --xuid 00090000AAAAAAAA --out <dir> <package>...`
   (license[1] = the XUID, title id `AAAAAAAA`), copied to
   `Content\0000000000000000\AAAAAAAA\00009000\`.

The XUID is the same for everyone (`kSharedOnlineXuid`), so items patched once
work for every converted profile on any console.

## Code

- PC build (`build_pc.ps1`: avtool, unit tests) and console build
  (`build_xbox.ps1`: xsdk in WSL; no Microsoft SDK).
- `core/`: STFS packages (LIVE/PIRS/CON and PEC): reads files, verifies block
  hashes and hash tables, replaces a file in place and rehashes it, console
  signature check (RSA) and thumbnail; Account decrypt/encrypt and the online
  XUID/Live conversion; profiles (`profile.h`: inspect, activate, apply a
  signature); GPD parsing incl. avatar awards; avatar item patching.
- `avtool`: `info`, `files`, `profile [--raw]`, `extract`, `awards`, `patch`,
  `set-xuid [--xuid] [--live]`, `verify-sig`.
- `app/` (xsdk): Avatar Thrift Store. Start menu "Add Items" / "Activate
  Offline Profile".
  - Items: game:\AvatarItems (any depth, folders as a list, items as tiles
    with their icon.png previews, NEW/UNLOCKED tags, folder search). "Add
    Item" / "Add Item and Remove" patch to title AAAAAAAA and license entry
    [1] = the shared XUID, [2]+ = each Xbox Live profile's online XUID (from
    its file, or from XamUserGetSigninInfo for a signed-in one); written to
    HDD Content\0000000000000000\AAAAAAAA\00009000. XUIDs an installed copy
    already has are kept; when "Add Items" opens, installed items get the
    XUIDs of Live profiles found since.
  - Profiles: HDD and USB (\Device\Harddisk0\Partition1, \Device\Mass0-2),
    all listed. A signed-in profile's file is held open by the system and
    can't be read; it is listed with what the system reports. Activation
    re-reads the profile, refuses signed-in ones (XamUserGetSigninInfo, xam
    0x227), backs it up to game:\Backups, rewrites it, signs with
    XeKeysConsolePrivateKeySign (xboxkrnl 0x256) and only writes if the
    signature verifies. The imports come from app/imports.txt through xsdk's
    gen_imports.py.
  - Font: app/font.h, rendered from Cascadia Mono (SIL OFL 1.1) by
    tools/gen_font.ps1.

## Formats

### Account file (in the profile, 0x194 bytes encrypted)
HMAC (0x10) | confounder (8) | data (0x17C), RC4/HMAC-SHA1 with the retail or
devkit key (`core/account.h`). Data:

| Offset | Field | Offline profile | Live-flagged (360mpGui / `--live`) |
|---|---|---|---|
| 0x00 | reserved flags: 0x20 Live, 0x10 passcode set (byte 0) | `00000001` | `20000001` |
| 0x08 | gamertag, UTF-16BE | gamertag | gamertag |
| 0x28 | online XUID | 0 | `00090000AAAAAAAA` |
| 0x30 | cached user flags (membership at 0x31) | 0 | 0 |
| 0x34 | service id | - | `PROD` |
| 0x38 | passcode (4 bytes) | - | - |
| 0x3C | online domain | - | `xbox.com` |
| 0x50 | Kerberos realm | - | `PASSPORT.NET` |
| 0x68 | 16 bytes key material | - | `81` then zeros |

The console rejects a Live-flagged profile while 0x68 is all zero (Test 8);
with 0x81 there it loads (Test 8b). Setting only "LIVE Enabled" in Horizon
breaks the profile the same way.

### CON signature (profiles)
From X360.dll (Le Fluffie, decompiled), checked on real profiles:
- Signed: SHA-1 of header 0x22C..0x344 (licenses, header hash, header size),
  RSA PKCS#1 v1.5, 1024-bit, exponent 65537.
- Console certificate (0x1A8) at 0x004, signature (0x80) at 0x1AC, stored
  byte-reversed. The cert's public key: exponent at 0x28, modulus at 0x2C with
  its 8-byte blocks in reverse order. Cert + signature are the 0x228 bytes
  `XeKeysConsolePrivateKeySign` returns.
- A profile's original signature is from the console that made it. Horizon
  resigns with a keyvault built into it (another console's), which RGH
  accepts but is probably why retail consoles call such profiles corrupt.

### Avatar item packages (LIVE)
- Avatar Friday Patcher writes the new title id at 0x360 and 0x3ED (end of the
  asset id at 0x3E1), the XUID as licensee of license[1] at 0x23C, and
  recomputes the header hash at 0x32C. `avtool patch` reproduces it exactly.
  A common placeholder licensee in item archives is `0009000000000000`.
- The LIVE signature covers the license entries too, so a patched item only
  works on a console that skips that check (RGH/JTAG; softmods untested).
- Further XUIDs go into free license entries [2]..[15] with entry [1]'s bits
  and flags; the console accepts them (a Live profile licensed in entry [2]
  can select the item).
- Asset id: component mask (4 bytes: 0x8 shirt, 0x1000 carryable, ...), 16
  bytes, ending in the title id. Award items have the award number in bytes
  4-5 (`00000008-000B-...`), marketplace items don't.
- Gender: `asset_v2.bin` byte 0x2D, 01 male, 02 female. The wrong gender hides
  an item.
- Items appear under System > Storage > Avatar Items whatever the license; the
  avatar editor only shows those licensed to the profile's online XUID.

### Profiles: awards
- Avatar award records: GPD namespace 6, in the title GPDs inside the PEC.
  Earned = flags 0x00130008 + unlock time; unearned = 0x00000008.
- Dashboard GPD (FFFE07D1.gpd) title entries hold award counters (0x16-0x1B).
- The profile's `AvatarAssets` folder is for items shown to others on Live
  (Horizon "insert new file", 1-2 files per item); not needed locally.

## Console tests (Ducktales Logo Tee)

"OfflineTest" is an offline profile, "LiveTest" a real Xbox Live profile.

| Test | Profile | Item license | Result |
|---|---|---|---|
| 1 | LiveTest | its online XUID only | shown, grey "!", not selectable |
| 3 | OfflineTest | unchanged | not shown |
| 6 | OfflineTest + online XUID, no Live flag | shared XUID | not shown |
| 7 | OfflineTest | offline XUID | not shown |
| 8 | OfflineTest + Live flag, 0x68 = 0 | shared XUID | profile rejected |
| 8b | OfflineTest + Live flag, 0x68 = 0x81 | shared XUID | **works**, equippable |
| 9 | LiveTest | shared XUID [1] + its online XUID [2] (app) | **works**, selectable |

Test 4 (console-bound license, licensee F000 + console id) was never run.

## Third-party tools analysed (none ever run)
- 360mpGui v1.5.0.0 (compiled AutoIt): script extracted statically; source of
  the 0x81 at 0x68. Another version of it was flagged by Windows Defender
  (Trojan:Win32/Malgent!MSR).
- Le Fluffie / X360.dll (.NET, GPL v3): source of the signing details. Phones
  home whenever it opens a package.
- Both bundle other consoles' keyvaults (KV.bin); never put one in this repo.

## Next steps
1. Activating an offline profile on the console. The first run reported "the
   console refused to sign": the app treated a zero return from
   XeKeysConsolePrivateKeySign as failure. The return value's meaning is
   unconfirmed, so the app now only logs it (with the output's first bytes,
   in AvatarThriftStore.log) and goes by whether the output verifies.
2. A Live profile that is signed in while items are added should get them
   too (its XUID comes from the system then).
3. What the grey "!" of Test 1 means (a Live profile with only its own XUID
   in entry [1]); with the shared XUID in [1] it doesn't appear.
4. Award items: mark PEC records as earned + update dashboard counters +
   rehash/resign PEC and profile.
