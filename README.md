# Avatar Thrift Store

The Xbox 360 avatar marketplace is closed, but its items still work. Avatar
Thrift Store is homebrew for modded Xbox 360 consoles that unlocks avatar item
packages for every profile on the console, including **offline profiles**,
which normally can't wear marketplace items at all. Everything happens on the
console; no PC tools needed. Built with [xsdk](https://github.com/xenosworks/xsdk),
no Microsoft SDK required.

- **Add Items**: browse a folder of item packages (with previews and search)
  and unlock them for every profile on the console.
- **Activate Offline Profile** *(beta)*: lets an offline profile wear unlocked
  items. Signed on the console with its own key, with a backup first.
- **Avatar Editor** *(beta)*: jumps straight to the dashboard's avatar editor.

Runs on RGH/JTAG consoles; softmods are untested. No avatar items are
included; bring your own.

## Getting started

1. Download the zip from [Releases](../../releases) and copy the
   `AvatarThriftStore` folder to the console, e.g. to `Hdd1:\`.
2. Put avatar item packages into `AvatarThriftStore\AvatarItems` (subfolders
   are fine, e.g. one per pack).
3. Start `AvatarThriftStore.xex` from Aurora or FSD.
4. Offline profiles: sign the profile out, then activate it under
   **Activate Offline Profile**. Xbox Live profiles need nothing.
5. **Add Items**, pick items (or *Add all items*), then **Avatar Editor** to
   put them on.

Controls: D-pad/left stick, A choose, B back, Y search, LB/RB page, BACK quit.
Items only show for an avatar of their gender.

## Key findings

### Avatar items

- An avatar item is a `LIVE` package. The avatar editor only shows items whose
  license names the profile's **online XUID**, so offline profiles, which have
  none, never see them.
- Unlocking an item means: title id `AAAAAAAA` at `0x360` and in the last 4
  bytes of the asset id (`0x3ED`), the XUID as licensee of license entry 1
  (entries: 16 x licensee/bits/flags at `0x22C`), and the header SHA-1 at
  `0x32C` recomputed. It goes to
  `Content\0000000000000000\AAAAAAAA\00009000\<asset id>`.
- The console accepts XUIDs in the other license entries (2-15) too, so **one
  installed item can be unlocked for several profiles**.
- The package's Microsoft signature no longer matches, which only modded
  kernels tolerate.

### Offline profiles

- An online XUID alone doesn't make items show: the profile also needs the
  Live flag. The Live flag alone makes the console reject the profile.
- What works is this set of values in the profile's `Account` file (0x194
  bytes, HMAC-SHA1/RC4-encrypted with the retail key):

  | Offset | Field | Value |
  |---|---|---|
  | `0x00` | reserved flags | `0x20000001` (Live-enabled) |
  | `0x28` | online XUID | `00090000AAAAAAAA` |
  | `0x34` | service id | `PROD` |
  | `0x3C` | online domain | `xbox.com` |
  | `0x50` | Kerberos realm | `PASSPORT.NET` |
  | `0x68` | key material | `0x81`, then zeros (all zeros: rejected) |

- The online XUID needn't be a real account's: every activated profile can
  share `00090000AAAAAAAA`, and items unlocked once work for all of them.

### Profile signatures

- Profiles are `CON` packages. The signature covers a SHA-1 of header bytes
  `0x22C-0x344` (license entries, header hash, header size); because the
  header hash covers everything below it, that hash stands for the whole
  profile. RSA-1024, PKCS#1 v1.5.
- The header holds 0x228 bytes at `0x004`: the signing console's certificate
  (0x1A8 bytes, with its public key) and the signature (0x80 bytes,
  byte-reversed).
- The console checks that Microsoft signed the certificate and that the
  signature matches it, not which console signed: profiles may move between
  consoles. Any genuine console key is accepted, the console's own included.
- The kernel signs with the console's own key on request:
  `XeKeysConsolePrivateKeySign` (xboxkrnl ordinal `0x256`) takes the 20-byte
  hash and returns the certificate and signature; the private key stays
  inside.

### The avatar editor

- The dashboard's avatar editor opens through `XamLaunchAvatarEditor` (xam
  ordinal `0x7DB`), not a separate program.
- It takes three arguments, `(userIndex, 0, 0)`; with only the user index it
  crashes. The system then closes the calling app and opens the editor for
  that user.

More details, formats and test results: [docs/STATUS.md](docs/STATUS.md).

## Building

**Console app** (WSL Debian with xsdk in `~/src/xsdk` and its toolchain in
`~/xsdk-gcc`, plus CMake, Ninja and Python 3):

```powershell
.\build_xbox.ps1                              # -> build\xbox\AvatarThriftStore.xex
.\tools\package_release.ps1 -Version 0.1.0    # -> dist\AvatarThriftStore-v0.1.0.zip
```

The title id is `41565453` ("AVTS"), so Aurora keeps the app's cover and
settings apart from other xsdk titles. The app writes a log next to itself
(`AvatarThriftStore.log`).

**PC tools** (Visual Studio with C++; uses its bundled CMake):

```powershell
.\build_pc.ps1    # avtool + unit tests
```

`avtool` does the same steps on a PC (`info`, `patch`, `set-xuid`,
`verify-sig`, ...); `set-xuid` doesn't sign, so resign its output with another
tool.

## License

MIT, see [LICENSE](LICENSE). Third-party code and the font:
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). No avatar items or other
Microsoft content are included.
