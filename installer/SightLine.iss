; Installs the standalone app into Program Files and the VST3 bundle into the
; shared VST3 folder, where every DAW finds it.

#define AppName "SightLine"
#define AppVersion "1.0.0"
#define AppPublisher "Codebuff"
#define AppURL "https://github.com/invot/Sightline"
#define AppExeName "SightLine.exe"

[Setup]
AppId={{4F0C7A2E-9B1D-4E63-A5C8-2D7E9F1B6A34}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=SightLine-Setup-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
Source: "..\build\SightLine_artefacts\Release\Standalone\SightLine.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\SightLine_artefacts\Release\VST3\SightLine.vst3\*"; DestDir: "{commoncf64}\VST3\SightLine.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
