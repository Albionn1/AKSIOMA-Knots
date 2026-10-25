<#
    Builds the Windows installer of AKSIOMA Knots.

    1. Release build of the program (CMake + Ninja, Qt's MinGW)
    2. The program and the Qt files it needs in one folder (windeployqt)
    3. The installer from AKSIOMAKnots.iss (Inno Setup 6.7 or newer)

    Result: installer\output\AKSIOMA-Knots-<version>-Setup.exe
    The version is the one in CMakeLists.txt (project(... VERSION x.y.z)).

    Build files go to installer\build (not in git).
#>
param(
    [string]$QtDir = "C:\Qt\6.12.0\mingw_64",
    [string]$QtTools = "C:\Qt\Tools",
    [string]$InnoSetup = ""
)

$ErrorActionPreference = "Stop"

$installer = $PSScriptRoot
$source = Split-Path $installer -Parent
$manuals = Join-Path (Split-Path $source -Parent) "Manual"
$build = Join-Path $installer "build\release"
$stage = Join-Path $installer "build\stage"

function Step($text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Cyan }

function Run($exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed (exit code $LASTEXITCODE)" }
}

# ---- tools ----

$mingw = Get-ChildItem $QtTools -Directory -Filter "mingw*_64" | Sort-Object Name -Descending | Select-Object -First 1
if (-not $mingw) { throw "No MinGW found in $QtTools" }

$env:PATH = @(
    (Join-Path $QtDir "bin"),
    (Join-Path $mingw.FullName "bin"),
    (Join-Path $QtTools "CMake_64\bin"),
    (Join-Path $QtTools "Ninja"),
    $env:PATH
) -join ";"

if (-not $InnoSetup) {
    $InnoSetup = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $InnoSetup) { throw "Inno Setup 6 not found (winget install JRSoftware.InnoSetup)" }

# ---- version ----

$cmakeLists = Get-Content (Join-Path $source "CMakeLists.txt") -Raw
if ($cmakeLists -notmatch 'project\(\s*AKSIOMAKnots\s+VERSION\s+([0-9.]+)') { throw "No version in CMakeLists.txt" }
$version = $Matches[1]

Write-Host "AKSIOMA Knots $version"
Write-Host "Qt:          $QtDir"
Write-Host "MinGW:       $($mingw.FullName)"
Write-Host "Inno Setup:  $InnoSetup"

# ---- 1. release build ----

Step "Release build"
Run "cmake" @("-S", $source, "-B", $build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=$QtDir")
Run "cmake" @("--build", $build)

# ---- 2. program folder ----

Step "Collecting the program and its Qt files"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item (Join-Path $build "AKSIOMAKnots.exe") $stage

# Qt's own translations are not needed (there is no Albanian one; the
# program's texts are built in)
Run "windeployqt" @("--release", "--compiler-runtime", "--no-translations", (Join-Path $stage "AKSIOMAKnots.exe"))

# ---- 3. installer ----

Step "Installer"
Run $InnoSetup @("/Q", "/DAppVersion=$version", "/DStageDir=$stage", "/DManualDir=$manuals", (Join-Path $installer "AKSIOMAKnots.iss"))

$setup = Join-Path $installer "output\AKSIOMA-Knots-$version-Setup.exe"
$size = [Math]::Round((Get-Item $setup).Length / 1MB, 1)

Write-Host ""
Write-Host "Done: $setup ($size MB)" -ForegroundColor Green
