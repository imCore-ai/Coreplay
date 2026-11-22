# Builds Coreplay on Windows (MSVC 2022 x64).
#   Qt 6       : -QtDir  (default C:\Qt\6.8.3\msvc2022_64)
#   vcpkg      : -Vcpkg  (default C:\vcpkg)  -> OpenImageIO + OpenColorIO from vcpkg.json
#   libmpv SDK : third_party\mpv (include\mpv\*.h, libmpv-2.dll, mpv.lib)
param(
    [string]$QtDir = "C:\Qt\6.8.3\msvc2022_64",
    [string]$Vcpkg = "C:\vcpkg",
    [string]$Config = "Release",
    [switch]$Tests,
    [switch]$Installer,          # also stage dist\Coreplay and build dist\Coreplay-<version>-Setup.exe
    [string]$Nsis = ""           # makensis.exe; default: PATH, else electron-builder's cached copy
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$build = Join-Path $root "build"

# Version: major.minor from CMakeLists.txt + the number of commits (each push is newer).
$mm = (Select-String -Path "$root\CMakeLists.txt" -Pattern 'project\(Coreplay VERSION (\d+)\.(\d+)').Matches[0].Groups
$count = (git -C $root rev-list --count HEAD).Trim()
$version = "$($mm[1].Value).$($mm[2].Value).$count"
Write-Host "Coreplay $version"

# Load the MSVC environment into this PowerShell session.
cmd /c "`"$vcvars`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}

# Switching between the release and the test build in one folder leaves Qt's generated
# moc code stale (the test driver's meta-object goes missing at link time): clear it.
$want = $(if ($Tests) { 'ON' } else { 'OFF' })
$cache = Join-Path $build "CMakeCache.txt"
if ((Test-Path $cache) -and -not (Select-String -Path $cache -Pattern "^COREPLAY_TESTS:BOOL=$want$" -Quiet)) {
    Remove-Item -Recurse -Force (Join-Path $build "coreplay_autogen") -ErrorAction SilentlyContinue
}

cmake -S $root -B $build -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCOREPLAY_VERSION_FULL=$version" `
    "-DCOREPLAY_TESTS=$(if ($Tests) { 'ON' } else { 'OFF' })" `
    -DCMAKE_TOOLCHAIN_FILE="$Vcpkg\scripts\buildsystems\vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows `
    -DCMAKE_PREFIX_PATH="$QtDir"
if ($LASTEXITCODE) { throw "configure failed" }
cmake --build $build
if ($LASTEXITCODE) { throw "build failed" }

# Self-contained folder: Qt runtime + libmpv next to coreplay.exe.
# opengl32sw.dll (software OpenGL) is shipped: Coreplay falls back to it when the GPU
# driver cannot give OpenGL 4.1 (no GPU, a remote session, a very old driver).
& "$QtDir\bin\windeployqt.exe" --no-translations --no-system-d3d-compiler "$build\coreplay.exe" | Out-Null
Copy-Item "$root\third_party\mpv\libmpv-2.dll" $build -Force
Write-Host "Built $build\coreplay.exe"

if ($Installer) {
    if ($Tests) { throw "Build the installer from a release build (no -Tests)." }
    # 1. A clean, self-contained app folder: no build files, no test tools.
    $dist = Join-Path $root "dist\Coreplay"
    if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
    New-Item -ItemType Directory -Force $dist | Out-Null
    Copy-Item "$build\coreplay.exe" $dist
    Get-ChildItem $build -Filter *.dll | Where-Object { $_.Name -ne "Qt6Test.dll" } | Copy-Item -Destination $dist
    foreach ($d in "platforms", "styles", "imageformats", "iconengines", "generic", "networkinformation", "tls") {
        if (Test-Path "$build\$d") { Copy-Item -Recurse "$build\$d" $dist }
    }
    # The Microsoft C++ runtime next to the exe: runs on a PC without Visual Studio, no admin.
    $crt = Get-ChildItem (Join-Path $env:VCToolsRedistDir "x64") -Directory -Filter "Microsoft.VC*.CRT" | Select-Object -First 1
    if (-not $crt) { throw "C++ runtime redistributable folder not found under $env:VCToolsRedistDir" }
    Copy-Item "$($crt.FullName)\*.dll" $dist

    # 2. The file types, then the installer.
    python -I "$root\installer\make_filetypes.py"
    if ($LASTEXITCODE) { throw "make_filetypes failed" }
    if (-not $Nsis) {
        $cmd = Get-Command makensis -ErrorAction SilentlyContinue
        if ($cmd) { $Nsis = $cmd.Source }
        else {
            $Nsis = Get-ChildItem "$env:LOCALAPPDATA\electron-builder\Cache" -Recurse -Filter makensis.exe -ErrorAction SilentlyContinue |
                Where-Object { $_.DirectoryName -notmatch '\\Bin$' } | Select-Object -First 1 -ExpandProperty FullName
        }
    }
    if (-not $Nsis -or -not (Test-Path $Nsis)) { throw "makensis.exe not found: pass -Nsis <path>" }
    $setup = Join-Path $root "dist\Coreplay-$version-Setup.exe"
    & $Nsis /V2 "/DVERSION=$version" "/DSRC=$dist" "/DOUTFILE=$setup" "$root\installer\coreplay.nsi"
    if ($LASTEXITCODE) { throw "makensis failed" }
    Write-Host "Built $setup"
}
