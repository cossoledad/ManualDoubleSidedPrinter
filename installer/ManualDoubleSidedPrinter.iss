#define MyAppName "ManualDoubleSidedPrinter"
#define MyAppVersion "1.0.4"
#define MyAppPublisher "ManualDoubleSidedPrinter"
#define MyAppExeName "ManualDoubleSidedPrinter.exe"

#ifndef MyAppPublishDir
  #define MyAppPublishDir "..\\artifacts\\publish\\win-x64"
#endif

#ifndef MyAppOutputDir
  #define MyAppOutputDir "..\\artifacts\\installer"
#endif

[Setup]
AppId={{D3B6E4AA-9D1C-4AD9-8FA2-0CD8F9AC7A01}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir={#MyAppOutputDir}
OutputBaseFilename=ManualDoubleSidedPrinter-Setup-{#MyAppVersion}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\Assets\app.ico

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#MyAppPublishDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\{#MyAppExeName}"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"; Flags: unchecked

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent
