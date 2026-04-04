$ErrorActionPreference = "Stop" # Exit immediately if a command fails

# 1. Setup paths
$TargetName = "ModemBridgeTray"
$AppName    = "ModemBridge"
$ReleaseDir = "..\release"
$BuildDir   = "..\build"
$WinTemp    = "$ReleaseDir\win-temp"

# Fallback to local paths if environment variables are not set
$CompilerPath = if ($env:MINGW_PATH) { $env:MINGW_PATH } else { "C:/Qt/Tools/mingw1310_64/bin" }
$QtBinPath    = if ($env:QT_BIN_PATH) { $env:QT_BIN_PATH } else { "C:/Qt/6.10.2/mingw_64/bin" }
$ISCC         = if ($env:ISCC_PATH) { $env:ISCC_PATH } else { "C:\Program Files (x86)\Inno\ISCC.exe" }
$Msys2Path    = if ($env:MSYS2_PATH) { $env:MSYS2_PATH } else { "C:\Qt\msys2" }

# 2. Setup Directories (Leaving build dir intact for faster rebuilds)
if (-Not (Test-Path $BuildDir)) { mkdir -p $BuildDir }
if (Test-Path $WinTemp) { Remove-Item -Recurse -Force $WinTemp }
mkdir -p $WinTemp

# 3. Configure and Build
Write-Host "Configuring with MinGW..." -ForegroundColor Cyan
& cmake -S .. -B $BuildDir -G "MinGW Makefiles" `
    -DCMAKE_PREFIX_PATH="$Msys2Path/ucrt64;$Msys2Path/mingw64" `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_MAKE_PROGRAM="$CompilerPath/mingw32-make.exe" `
    -DCMAKE_CXX_COMPILER="$CompilerPath/g++.exe" `
    -DCMAKE_C_COMPILER="$CompilerPath/gcc.exe" `
    -DCMAKE_RC_COMPILER="$CompilerPath/windres.exe" 

Write-Host "Building $TargetName..." -ForegroundColor Cyan
& cmake --build $BuildDir --config Release -j ($env:NUMBER_OF_PROCESSORS)

# 4. Stage and Deploy
Write-Host "Running windeployqt..." -ForegroundColor Cyan
# Copy the compiled target and rename it to the desired application name
Copy-Item "$BuildDir\$TargetName.exe" "$WinTemp\$AppName.exe"
& "$QtBinPath\windeployqt.exe" --dir "$WinTemp" "$WinTemp\$AppName.exe"

# 5. Create Portable ZIP
Write-Host "Creating Portable ZIP..." -ForegroundColor Cyan
Compress-Archive -Path "$WinTemp\*" -DestinationPath "$ReleaseDir\$AppName-Portable.zip" -Force

# 6. Create Installer
if (Test-Path $ISCC) {
    Write-Host "Compiling Inno Setup Installer..." -ForegroundColor Cyan
    & $ISCC "ModemBridge-Installer.iss"
    Write-Host "Installer created successfully in $ReleaseDir!" -ForegroundColor Green
} else {
    Write-Host "SKIPPED: Inno Setup 6 not found at $ISCC" -ForegroundColor Yellow
    Write-Host "Download from https://jrsoftware.org/isdl.php to generate the Setup.exe" -ForegroundColor Yellow
}

Write-Host "Deployment Complete!" -ForegroundColor Green
