# make_package.ps1: builds the portable package a friend can use (LBP3Launcher.exe + lbp3.exe) and zips it.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_package.ps1 [-Version v0.1.0-lbp3port]
#       [-OutRoot D:\Recomps\lbp3-dist] [-SkipBuild] [-AllowDirty]
# The package NEVER contains game content (game files, DLC, shader caches, saves, art from the dump) or anyone's
# account files: the last step scans the staged folder and refuses to zip if anything like that slipped in.
# Layout: one folder, everything relative to it (portable.txt marks it for the launcher):
#   LBP3Launcher.exe, lbp3.exe, SDL3.dll, the VC++ runtime DLLs (app-local), tools\nid_names.txt,
#   portable.txt, README-FRIENDS.md, CREDITS.md, SOURCE.md, licenses\
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

if (-not $SkipBuild) {
    cmd /d /c build.bat
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
Copy-Item tools\package\README-FRIENDS.md, tools\package\CREDITS.md $stage
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
$libs = "abseil-cpp", "ext-boost", "fmt", "glslang", "half", "imgui", "libressl", "magic_enum", "miniz", "protobuf",
        "robin-map", "sirit", "tracy", "vma", "vulkan-headers", "xbyak", "xxhash", "zydis"
Copy-Item third_party\shadPS4\LICENSE* "$stage\licenses\third_party\" -ErrorAction SilentlyContinue
Get-ChildItem "$stage\licenses\third_party" -File | ForEach-Object { Rename-Item $_.FullName ("shadPS4-" + $_.Name) }
foreach ($lib in $libs) {
    $files = Get-ChildItem "$ext\$lib" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^(LICEN[CS]E|COPYING|COPYRIGHT|NOTICE)' }
    if ($files) {
        New-Item -ItemType Directory -Force "$stage\licenses\third_party\$lib" | Out-Null
        $files | ForEach-Object { Copy-Item $_.FullName "$stage\licenses\third_party\$lib\" }
    }
}
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
Compress-Archive -Path "$stage\*" -DestinationPath $zip -CompressionLevel Optimal
$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
Set-Content -Encoding ascii "$zip.sha256" "$hash  $name.zip"
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "package: $zip ($size MB)"
Write-Host "sha256:  $hash"
Write-Host "source:  $remote @ $commit ($branch); shadPS4 @ $subCommit"
