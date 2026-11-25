#define MyAppName "ManualDoubleSidedPrinter"
#define MyAppExeName "ManualDoubleSidedPrinter.exe"

[Setup]
AppId={{D3B6E4AA-9D1C-4AD9-8FA2-0CD8F9AC7A01}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir={#MyAppOutputDir}
OutputBaseFilename=ManualDoubleSidedPrinter-Setup-{#MyAppVersion}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\Assets\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}

[Files]
Source: "{#MyAppPublishDir}\{#MyAppExeName}"; DestDir: "{app}"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
