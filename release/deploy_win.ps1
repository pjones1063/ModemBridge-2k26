$ErrorActionPreference = "Stop" # Exit immediately if a command fails

# 1. Setup paths
$TargetName = "ModemBridgeTray"
$AppName    = "ModemBridge"
$ReleaseDir = "..\release"
$BuildDir   = "..\build"
$WinTemp    = "$ReleaseDir\win-temp"

# Define Toolchain Paths (Identical to your working AspeQt script)
$CompilerPath = "C:/Qt/Tools/mingw1310_64/bin"
$QtBinPath    = "C:/Qt/6.10.2/mingw_64/bin"
$ISCC         = "C:\Program Files (x86)\Inno\ISCC.exe"
$Msys2Path    = "C:\Qt\msys2" # <--- Added MSYS2 Base Path

# 2. Hard Clean
# Remove the build directory to clear the old CMakeCache.txt
if (Test-Path $BuildDir) {
    Write-Host "Cleaning old build directory..." -ForegroundColor Cyan
    Remove-Item -Recurse -Force $BuildDir
}
mkdir -p $BuildDir

if (Test-Path $WinTemp) { 
    Remove-Item -Recurse -Force $WinTemp 
}
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

Write-Host "Building $AppName..." -ForegroundColor Cyan
& cmake --build $BuildDir --config Release -j ($env:NUMBER_OF_PROCESSORS)

# Stop the script if the build failed
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE. Check the errors above."
    exit $LASTEXITCODE
}

# 4. Stage and Deploy
Write-Host "Running windeployqt..." -ForegroundColor Cyan
# Copy the compiled target and rename it to the desired application name
Copy-Item "$BuildDir\$TargetName.exe" "$WinTemp\$AppName.exe"
& "$QtBinPath\windeployqt.exe" --dir "$WinTemp" "$WinTemp\$AppName.exe"

# <--- NEW: Copy ALL required MSYS2 dependencies so the app actually launches
Write-Host "Bundling MSYS2 Dependencies..." -ForegroundColor Cyan

# List all the non-Qt MSYS2 DLLs your app needs here
$MsysDlls = @("libssh.dll", "libcrypto-3-x64.dll", "zlib1.dll")

foreach ($dll in $MsysDlls) {
    $found_dll = Get-ChildItem -Path "$Msys2Path\ucrt64\bin", "$Msys2Path\mingw64\bin" -Filter $dll -ErrorAction SilentlyContinue | Select-Object -First 1

    if ($found_dll) {
        Copy-Item $found_dll.FullName "$WinTemp\"
        Write-Host " -> Copied $($found_dll.Name)" -ForegroundColor Green
    } else {
        Write-Warning "Could not find $dll in $Msys2Path. You may need to copy it manually!"
    }
}

# 5. Create Portable ZIP
Write-Host "Creating Portable ZIP..." -ForegroundColor Cyan
Compress-Archive -Path "$WinTemp\*" -DestinationPath "$ReleaseDir\$AppName-Portable.zip" -Force

# 6. Create Installer (Inno Setup)
if (Test-Path $ISCC) {
    Write-Host "Compiling Inno Setup Installer..." -ForegroundColor Cyan
    & $ISCC "ModemBridge-Installer.iss"
    Write-Host "Installer created successfully in $ReleaseDir!" -ForegroundColor Green
} else {
    Write-Warning "Inno Setup Compiler not found at $ISCC. Skipping installer."
}

Write-Host "Deployment Complete! Files are in the release folder." -ForegroundColor Green