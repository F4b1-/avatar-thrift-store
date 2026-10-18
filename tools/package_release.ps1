# Packs a release zip from the console build:
#   AvatarThriftStore\AvatarThriftStore.xex
#   AvatarThriftStore\AvatarItems\          (empty, with a note; items aren't shipped)
#   AvatarThriftStore\README.txt, LICENSE.txt, THIRD_PARTY_NOTICES.md
#   .\tools\package_release.ps1 -Version 0.1.0     -> dist\AvatarThriftStore-v0.1.0.zip
# Build first with .\build_xbox.ps1. Entry names use forward slashes, which
# every unzip tool understands (Compress-Archive in Windows PowerShell 5.1
# writes backslashes).
param(
    [Parameter(Mandatory)] [string]$Version
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$xex = Join-Path $root "build\xbox\AvatarThriftStore.xex"
if (-not (Test-Path $xex)) { throw "no console build: run .\build_xbox.ps1 first" }

$readme = @"
Avatar Thrift Store v$Version
https://github.com/F4b1-/avatar-thrift-store

Homebrew for modded Xbox 360 consoles (RGH/JTAG; softmods untested) that
unlocks avatar items for every profile on the console.

Setup
1. Copy the AvatarThriftStore folder to the hard drive (e.g. Hdd1:\) or a
   USB stick. With both, you pick in the unlock dialog where unlocked items go.
2. Put avatar item packages into AvatarThriftStore\AvatarItems (subfolders
   are fine, e.g. one per pack). No items come with the app.
3. Start AvatarThriftStore.xex from Aurora/FSD.

Unlock Items              unlock items for every Xbox Live profile and
                          every profile activated here.
Activate Offline Profile  lets an offline profile wear unlocked items.
                          Sign the profile out first; a backup goes to
                          AvatarThriftStore\Backups.
Avatar Editor             opens the dashboard's avatar editor.

Pull both triggers for a screenshot: a PNG in AvatarThriftStore\Screenshots.
The app writes a log next to itself (AvatarThriftStore.log).
"@

$itemsNote = @"
Put avatar item packages here (subfolders are fine, e.g. one per pack).
The app lists everything in this folder; no items come with it.
"@

$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force $dist | Out-Null
$zipPath = Join-Path $dist "AvatarThriftStore-v$Version.zip"
if (Test-Path $zipPath) { Remove-Item -LiteralPath $zipPath -Confirm:$false }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    function Add-File($source, $name) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $source, $name) | Out-Null
    }
    function Add-Text($text, $name) {
        $entry = $zip.CreateEntry($name)
        $writer = New-Object System.IO.StreamWriter($entry.Open(), (New-Object System.Text.UTF8Encoding($false)))
        $writer.Write(($text -replace "`r?`n", "`r`n"))
        $writer.Dispose()
    }
    Add-File $xex "AvatarThriftStore/AvatarThriftStore.xex"
    Add-Text $itemsNote "AvatarThriftStore/AvatarItems/Put avatar items here.txt"
    Add-Text $readme "AvatarThriftStore/README.txt"
    Add-File (Join-Path $root "LICENSE") "AvatarThriftStore/LICENSE.txt"
    Add-File (Join-Path $root "THIRD_PARTY_NOTICES.md") "AvatarThriftStore/THIRD_PARTY_NOTICES.md"
}
finally {
    $zip.Dispose()
}
Write-Host "Wrote $zipPath"
