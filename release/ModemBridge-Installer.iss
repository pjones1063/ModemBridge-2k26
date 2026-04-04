[Setup]
AppName=ModemBridge
AppVersion=1.0.0
DefaultDirName={autopf}\ModemBridge
OutputDir=..\release
OutputBaseFilename=ModemBridge-Setup

[Files]
; Source files staged in win-temp by the script
Source: "win-temp\ModemBridge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "win-temp\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\ModemBridge"; Filename: "{app}\ModemBridge.exe"
Name: "{autodesktop}\ModemBridge"; Filename: "{app}\ModemBridge.exe"