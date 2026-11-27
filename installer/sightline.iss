; Installs the standalone app into Program Files and the VST3 bundle into the
; shared VST3 folder, where every DAW finds it.

#define AppName "sightline"
#define AppVersion "1.0.0"
#define AppPublisher "Codebuff"
#define AppURL "https://github.com/invot/sightline"
#define AppExeName "sightline.exe"

[Setup]
AppId={{4F0C7A2E-9B1D-4E63-A5C8-2D7E9F1B6A34}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
LicenseFile=..\LICENSE
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
OutputBaseFilename=sightline-Setup-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
Source: "..\build\SightLine_artefacts\Release\Standalone\sightline.exe"; DestDir: "{app}"; Flags: ignoreversion
; The hand landmark model runs on ONNX Runtime, which sits beside the app (and
; inside the VST3 bundle, which the line below copies whole).
Source: "..\build\SightLine_artefacts\Release\Standalone\onnxruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\SightLine_artefacts\Release\Standalone\*.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\SightLine_artefacts\Release\VST3\sightline.vst3\*"; DestDir: "{commoncf64}\VST3\sightline.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
