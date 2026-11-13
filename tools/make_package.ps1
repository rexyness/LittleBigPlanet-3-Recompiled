# make_package.ps1: builds the portable package a friend can use (LBP3Launcher.exe + lbp3.exe) and zips it.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_package.ps1 [-Version v0.1.0-lbp3port]
#       [-OutRoot D:\Recomps\lbp3-dist] [-SkipBuild] [-AllowDirty]
# The package NEVER contains game content (game files, DLC, shader caches, saves, art from the dump) or anyone's
# account files: the last step scans the staged folder and refuses to zip if anything like that slipped in.
# Layout: one folder, everything relative to it (portable.txt marks it for the launcher):
#   LBP3Launcher.exe, lbp3.exe, SDL3.dll, the VC++ runtime DLLs (app-local), tools\nid_names.txt,
#   portable.txt, README-FRIENDS.md, CREDITS.md, RELEASE-NOTES.md (also the GitHub release's text; the
#   launcher's "What's new" shows it), SOURCE.md, licenses\
# Make release packages in a dedicated worktree checked out at the exact (pushed) commit, e.g.
#   git worktree add --detach D:\Recomps\lbp3-wt\release <commit>   (shadPS4 at its submodule commit)
# so the build is that commit's and nobody else's build folder is touched.
#   (user\ for settings, saves and the shader cache, and analysis\ for logs, are made on first run)
param(
    [string]$Version = "v0.1.0-lbp3port",
    [string]$OutRoot = "D:\Recomps\lbp3-dist",
    [switch]$SkipBuild,
    [switch]$AllowDirty
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

# The source a release must point at (GPL): this checkout's commit and its shadPS4 submodule's.
$dirty = (git status --porcelain --ignore-submodules=all | Where-Object { $_ -notmatch '^\?\? ' })
if ($dirty -and -not $AllowDirty) { throw "the working tree has uncommitted changes; commit first (or -AllowDirty for a test package)" }
$commit = (git rev-parse HEAD).Trim()
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
$subCommit = (git -C third_party/shadPS4 rev-parse HEAD).Trim()
$remote = "https://github.com/rexyness/LittleBigPlanet-3-Recompiled"
$subRemote = "https://github.com/rexyness/shadPS4"
# The commit must be public (on a branch of the fork remote), and SOURCE.md names that branch. A release
# worktree is checked out detached at the exact commit, so its own branch name ("HEAD") isn't used.
try { git fetch -q fork 2>$null } catch { }
$onFork = @(git branch -r --contains $commit --format "%(refname:short)" | Where-Object { $_ -like 'fork/*' } |
    ForEach-Object { $_.Substring(5) })
if (-not $onFork) {
    if (-not $AllowDirty) { throw "commit $commit isn't on any branch of the fork remote; push it first (the source must be public)" }
    Write-Host "warning: commit $commit isn't on the fork (test package)"
} elseif ($onFork -notcontains $branch) {
    $branch = if ($onFork -contains 'bestofboth') { 'bestofboth' } else { $onFork[0] }
}

if (-not $SkipBuild) {
    # By full path: with NoDefaultCurrentDirectoryInExePath set, cmd doesn't look in the current folder.
    & "$repo\build.bat"
    if ($LASTEXITCODE -ne 0) { throw "build.bat failed" }
}
foreach ($f in "build\lbp3.exe", "build\LBP3Launcher.exe", "build\SDL3.dll", "tools\nid_names.txt") {
    if (-not (Test-Path $f)) { throw "missing $f" }
}

# The VC++ runtime (msvcp140, vcruntime140, ...), copied app-local from Visual Studio's redist folder (the
# redistributable licence allows that), so a friend needs no separate install.
$vs = "C:\Program Files\Microsoft Visual Studio\18\Community"
$crt = Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT" -Directory -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crt) { throw "no VC++ runtime (Microsoft.VC14*.CRT) under $vs\VC\Redist\MSVC" }

$name = "LBP3-Recompiled-$Version"
$stage = Join-Path $OutRoot $name
$zip = Join-Path $OutRoot "$name.zip"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage, "$stage\tools", "$stage\licenses\third_party" | Out-Null

Copy-Item build\lbp3.exe, build\LBP3Launcher.exe, build\SDL3.dll $stage
Copy-Item tools\nid_names.txt "$stage\tools\"
foreach ($dll in "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "msvcp140_atomic_wait.dll",
                 "vcruntime140.dll", "vcruntime140_1.dll") {
    if (Test-Path "$($crt.FullName)\$dll") { Copy-Item "$($crt.FullName)\$dll" $stage }
}
Set-Content -Encoding ascii "$stage\portable.txt" @"
This folder is a portable LittleBigPlanet 3 Recompiled install. The launcher keeps everything here:
user\ (settings, saves, shader cache) and analysis\ (logs, save backups). Delete this file only if you
move LBP3Launcher.exe into a development checkout.
"@

# Docs: the friend's README, credits, licence note (tools\package\), and SOURCE.md with the exact commits.
Copy-Item tools\package\README-FRIENDS.md, tools\package\CREDITS.md, tools\package\RELEASE-NOTES.md $stage
Copy-Item tools\package\LICENSES.md, tools\package\GPL-3.0.txt "$stage\licenses\"
Copy-Item LICENSE "$stage\licenses\LICENSE-LBP3-Recompiled.txt"
Set-Content -Encoding utf8 "$stage\SOURCE.md" @"
# Where's the source

This package is free software under the GNU GPL (see licenses\). Its complete corresponding source is public:

- LittleBigPlanet 3 Recompiled (this port): $remote
  branch ``$branch``, commit ``$commit``
- shadPS4 (the graphics core, a submodule of the above): $subRemote
  commit ``$subCommit``

Clone the first with ``git clone --recursive $remote``, check out that commit, and build with ``build.bat``
(Visual Studio 2026 with clang-cl). Package version: $Version.
"@

# Third-party licences: the shadPS4 core and every library the build links (runtime/CMakeLists.txt, cmake/*.cmake).
$ext = "third_party\shadPS4\externals"
$libs = "abseil-cpp", "ext-boost", "fmt", "glslang", "half", "imgui", "json", "libressl", "magic_enum", "miniz", "protobuf",
        "robin-map", "sirit", "tracy", "vma", "vulkan-headers", "xbyak", "xxhash", "zydis"
New-Item -ItemType Directory -Force "$stage\licenses\third_party\shadPS4" | Out-Null
Copy-Item third_party\shadPS4\LICENSE "$stage\licenses\third_party\shadPS4\LICENSE"
Copy-Item third_party\shadPS4\LICENSES\*.txt "$stage\licenses\third_party\shadPS4\" -ErrorAction SilentlyContinue
foreach ($lib in $libs) {
    $files = Get-ChildItem "$ext\$lib" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^(LICEN[CS]E|COPYING|COPYRIGHT|NOTICE)' -and $_.Extension -ne '.cfg' }
    if ($files) {
        New-Item -ItemType Directory -Force "$stage\licenses\third_party\$lib" | Out-Null
        $files | ForEach-Object { Copy-Item $_.FullName "$stage\licenses\third_party\$lib\" }
    }
}
# stb (stb_image in the launcher and jpeg.cpp) keeps its licence at the end of the header: copy that block.
$stbLines = Get-Content "$ext\stb\stb_image.h"
$stbAt = ($stbLines | Select-String -SimpleMatch "This software is available under 2 licenses" | Select-Object -First 1).LineNumber
if (-not $stbAt) { throw "no licence block found in stb_image.h" }
New-Item -ItemType Directory -Force "$stage\licenses\third_party\stb" | Out-Null
$stbLines[($stbAt - 1)..($stbLines.Count - 1)] | Where-Object { $_ -notmatch '^\*/\s*$' } |
    Set-Content -Encoding ascii "$stage\licenses\third_party\stb\LICENSE.txt"
$sdl = Get-ChildItem third_party\SDL3 -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^LICEN[CS]E' }
if ($sdl) { New-Item -ItemType Directory -Force "$stage\licenses\third_party\SDL3" | Out-Null; $sdl | Copy-Item -Destination "$stage\licenses\third_party\SDL3\" }

# Safety: nothing from the game, the user's saves or accounts may be in the package.
$forbidden = Get-ChildItem $stage -Recurse -File | Where-Object {
    $_.Name -match '(?i)^(eboot\.bin|param\.sfo|game_dir\.txt|launcher\.ini|online_account.*\.txt|input\.ini|driver\.vkcache)$' -or
    $_.Extension -match '(?i)^\.(farc|pkg|sprx|prx|spv|fsb|bik|bin|pdb)$' -or
    $_.FullName -match '(?i)\\(savedata|shader_cache[^\\]*|sce_sys|CUSA\d{5}[^\\]*|user|analysis)\\'
}
if ($forbidden) {
    $forbidden | ForEach-Object { Write-Host "FORBIDDEN in package: $($_.FullName)" }
    throw "the package would contain game content or personal files; nothing was zipped"
}

if (Test-Path $zip) { Remove-Item -Force $zip }
# Not Compress-Archive: Windows PowerShell 5.1's writes entry names with backslashes (licenses\...), which
# the zip format doesn't allow (Info-ZIP warns, other unzippers make files named "licenses\x"). The
# entries are written here with forward slashes, files only, in a fixed order.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$stageFull = (Resolve-Path $stage).Path.TrimEnd('\')
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    Get-ChildItem $stageFull -Recurse -File | Sort-Object FullName | ForEach-Object {
        $entry = $_.FullName.Substring($stageFull.Length + 1).Replace('\', '/')
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $entry,
            [System.IO.Compression.CompressionLevel]::Optimal)
    }
} finally { $archive.Dispose() }
$check = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    $bad = @($check.Entries | Where-Object { $_.FullName.Contains('\') })
    $count = $check.Entries.Count
} finally { $check.Dispose() }
if ($bad) { throw "zip entries with backslashes: $($bad[0].FullName)" }
if ($count -ne (Get-ChildItem $stageFull -Recurse -File).Count) { throw "the zip has $count entries, the staged folder a different number of files" }
# (.NET's SHA256, not Get-FileHash: that cmdlet's module isn't found when started from Git Bash)
$sha = [System.Security.Cryptography.SHA256]::Create()
$fs = [System.IO.File]::OpenRead($zip)
$hash = -join ($sha.ComputeHash($fs) | ForEach-Object { $_.ToString("x2") })
$fs.Dispose(); $sha.Dispose()
Set-Content -Encoding ascii "$zip.sha256" "$hash  $name.zip"
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "package: $zip ($size MB)"
Write-Host "sha256:  $hash"
Write-Host "source:  $remote @ $commit ($branch); shadPS4 @ $subCommit"
