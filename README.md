# Avatar Thrift Store

Homebrew for modded Xbox 360 consoles (RGH/JTAG; softmods untested) that
unlocks downloaded avatar items for every profile on the console and prepares
offline profiles to use them, directly on the console. Built with
[xsdk](https://github.com/xenosworks/xsdk), no Microsoft SDK needed.

**Status:** unlocking items works on the console, for activated offline
profiles and Xbox Live profiles (docs/STATUS.md). Activating an offline
profile on the console itself (its signing step) is not confirmed yet.

## Using the app

1. Copy `AvatarThriftStore.xex` to a folder on the console, e.g.
   `Hdd1:\AvatarThriftStore\`, and put avatar item packages in an
   `AvatarItems` folder next to it (`Hdd1:\AvatarThriftStore\AvatarItems\`).
2. Launch the app from Aurora/FSD. To activate a profile, sign it out first:
   a signed-in profile's file is held open by the system.
3. **Activate Offline Profile** (once per offline profile): lists every
   profile on the hard disk and USB storage. Activated and Xbox Live profiles
   read "ready for items". An offline profile gets the shared online XUID and
   is re-signed with this console's own key; a copy of the original goes to
   `Backups\` next to the app first. Restart the console afterwards.
4. **Add Items**: subfolders (any depth, e.g. one per pack) as a list, items
   below them as tiles with their 128 x 128 previews, all sorted by name; a
   tag says NEW or UNLOCKED, and the selected item's full name and state show
   under the grid. A opens a folder, B goes back up. *Add Item* patches the
   package and copies it to `Content\0000000000000000\AAAAAAAA\00009000\` on
   the hard disk; *Add Item and Remove* also deletes it from `AvatarItems`.
   The first row, *Add all items*, does the same for every file in the
   current folder and its subfolders. An unlocked item works for every
   activated profile and every Xbox Live profile on the console; items
   unlocked before a Live profile turned up get it when Add Items opens.
5. **Search** (Y): an on-screen keyboard searches the names of all folders
   below `AvatarItems`, at any depth; every typed word must appear. Matches
   show while typing; *Show results* (or START) lists them, A opens one and B
   goes back to the results, then to where the search started.

Controls: D-pad or left stick, LB/RB a page, A choose, B back, Y search, BACK
quit; on the keyboard X deletes and Y types a space. Items only show for an
avatar of their gender.

## How it works

Offline profiles get a shared online XUID (`00090000AAAAAAAA`) and are marked
Live-enabled, the way 360mpGui's offline-to-online maker does it. Avatar items
are licensed to that XUID and to each Xbox Live profile's own, so items
patched once work for every such profile. The same steps on the PC:

```powershell
.\build\pc\Release\avtool.exe set-xuid --live --out <new profile> <profile>
.\build\pc\Release\avtool.exe patch --xuid 00090000AAAAAAAA --out <dir> <item> [...]
.\build\pc\Release\avtool.exe verify-sig <profile>
```

`set-xuid` doesn't sign: resign its output (Horizon: save, rehash and
resign) before copying it back to `Content\<offline xuid>\FFFE07D1\00010000\`.

## Layout

| Path              | Contents                                                       |
|-------------------|----------------------------------------------------------------|
| `core/`           | Portable C++ (PC and console): STFS, Account, profiles, items, SHA-1, RSA check |
| `app/`            | Console app (xsdk): UI, items, profiles, drives                |
| `tools/avtool/`   | PC command-line tool for inspecting and processing packages    |
| `tools/gen_font.ps1` | Renders the app's bitmap font (`app/font.h`)                |
| `tests/`          | PC unit tests for `core`                                       |
| `third_party/`    | `stb_image.h` v2.30 (public domain / MIT), for item thumbnails |

## Building

**PC** (VS 2026 with C++ tools; uses its bundled CMake):

```powershell
.\build_pc.ps1              # avtool + unit tests
```

**Xbox 360** (WSL Debian with xsdk in `~/src/xsdk` and its toolchain in
`~/xsdk-gcc`, plus CMake, Ninja and Python 3):

```powershell
.\build_xbox.ps1            # -> build\xbox\AvatarThriftStore.xex
```

Copy the `.xex` to the console via FTP and launch it from Aurora/FSD. The app
writes a log next to itself (`AvatarThriftStore.log`).

## License

MIT (`LICENSE`). Third-party code and the font: `THIRD_PARTY_NOTICES.md`.
No avatar items or other Microsoft content are included; bring your own.

## Local folders (git-ignored)

- `samples/`: test item packages.
- `profiles/`: profile copies from the console; keep an untouched backup.
- `reference/`: reference profiles. Never commit a `KV.bin` (another
  console's keys).
