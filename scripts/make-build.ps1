<#
    MegaPPBox - package a runnable build folder.

        <Root>\NN-short-description\
            MegaPPBox.exe
            roms\          <- the ROM set (board BIOSes, video BIOSes, U12, settled CMOS)
            MegaPPBox.cfg  <- only with -Library: points the Machine Manager at a folder
            BUILD.txt

    A folder is one cabinet: its settings (MegaPPBox.cfg) and CMOS (nvr\) are
    kept inside it, so folders do not share state.  The security keys are
    built into the executable; a keys\ folder is only for the user's own.  The manager
    runs the images it lists in place, and games write to their disks
    (NVRAM.DAT, DEBUG.DAT), so point -Library at working copies, never at
    original images.

    Usage:
        pwsh scripts\make-build.ps1 -Name "first-manager" [-Library "F:\...\masters"] [-Root <dir>]
#>
param(
    [Parameter(Mandatory = $true)][string] $Name,
    [string] $Library,
    [string] $Root = (Join-Path (Split-Path $PSScriptRoot -Parent) "..\MegaPPBox-builds")
)
$ErrorActionPreference = "Stop"

$repo = Split-Path $PSScriptRoot -Parent
$exe  = Join-Path $repo "build\src\MegaPPBox.exe"
if (-not (Test-Path $exe)) { throw "No build at $exe - build first." }

New-Item -ItemType Directory -Force -Path $Root | Out-Null
$Root = (Resolve-Path $Root).Path

# Number the folders so they sort in the order they were made.
$next = 1
$existing = Get-ChildItem $Root -Directory | Where-Object { $_.Name -match '^(\d+)-' } |
            ForEach-Object { [int]($_.Name -split '-')[0] }
if ($existing) { $next = ($existing | Measure-Object -Maximum).Maximum + 1 }
$dir = Join-Path $Root ($next.ToString("00") + "-" + $Name)
New-Item -ItemType Directory -Path $dir | Out-Null

Copy-Item $exe $dir
Copy-Item (Join-Path $repo "roms") $dir -Recurse

if ($Library) {
    # Settings files are UTF-8 without a byte-order mark.
    [System.IO.File]::WriteAllText((Join-Path $dir "MegaPPBox.cfg"),
        "[MegaPPBox]`r`nlibrary = $Library`r`n", (New-Object System.Text.UTF8Encoding($false)))
}

$commit  = (& git -C $repo rev-parse --short HEAD).Trim()
$subject = (& git -C $repo log -1 --pretty=%s).Trim()
$dirty   = if (& git -C $repo status --porcelain -- src) { " (plus uncommitted changes)" } else { "" }
@"
MegaPPBox build $($next.ToString("00")) - $Name
Commit:  $commit $subject$dirty
Made:    $(Get-Date -Format "yyyy-MM-dd HH:mm")
Library: $(if ($Library) { $Library } else { "(none - choose one in the Machine Manager)" })

Run MegaPPBox.exe.  With no image picked yet, the Machine Manager opens first.
"@ | Set-Content (Join-Path $dir "BUILD.txt") -Encoding utf8

"Staged $dir"
