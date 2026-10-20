# Builds the console app (build\xbox\AvatarThriftStore.xex) from Windows: runs build_xbox.sh in WSL Debian.
#   .\build_xbox.ps1 [-Xsdk <WSL path of another xsdk checkout>]
# See build_xbox.sh for what it needs.
param(
    [string]$Xsdk = ""
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
# C:\Users\... -> /mnt/c/Users/...
$wslRoot = "/mnt/" + $root.Substring(0, 1).ToLower() + ($root.Substring(2) -replace '\\', '/')

$arguments = @("$wslRoot/build_xbox.sh")
if ($Xsdk) { $arguments += $Xsdk }
& wsl.exe -d Debian --exec bash @arguments
if ($LASTEXITCODE) { throw "xsdk build failed" }
