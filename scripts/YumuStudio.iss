; Yumu Studio Installer Script (Inno Setup)
; Run this script with Inno Setup Compiler after building the portable package:
;   scripts\make_portable.bat
;   ISCC scripts\YumuStudio.iss

#define MyAppName "Yumu Studio"
#define MyAppVersion "2.0.0"
#define MyAppPublisher "YumuStudio"
#define MyAppExeName "YumuStudio.exe"
#define MyAppSourceDir "..\YumuStudio_Portable"

[Setup]
AppId={{A7B3C4D5-E6F7-8901-2345-6789ABCDEF01}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=no
OutputDir=..\Releases
OutputBaseFilename=YumuStudio-{#MyAppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequiredOverridesAllowed=dialog
SetupIconFile=app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoProductName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon"; GroupDescription: "Additional icons:"; Flags: unchecked
Name: "launch"; Description: "&Launch Yumu Studio after installation"; GroupDescription: "Other tasks:"

[Files]
Source: "{#MyAppSourceDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; Models and engines (torch/CUDA/whisper.cpp/model.bin, several GB) are downloaded
; by the app at runtime, so they are excluded to keep a single Setup.exe under
; Inno's ~4.2 GB one-file limit. *.pyc bytecode caches are regenerated on demand.
Source: "{#MyAppSourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs; Excludes: "\engines,\models,*.pyc,portable.flag"
; NOTE: Do not use "Flags: ignoreversion" on any shared system files.

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent; Tasks: launch

[UninstallDelete]
; Keep user downloads/settings? The portable package stores them inside the app folder by default.
; Uncomment the next line to also remove engines/models/settings on uninstall.
; Type: filesandordirs; Name: "{app}\config"
; Type: filesandordirs; Name: "{app}\engines"
; Type: filesandordirs; Name: "{app}\models"
