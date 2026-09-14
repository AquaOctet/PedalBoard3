; Pedalboard 3 - the Windows installer (Inno Setup 6, #88).
;
; Where things go (decided 2026-09-14):
;   - the app: C:\Program Files\Pedalboard 3 (per machine, so it needs admin)
;   - shared data for every user of the PC: C:\ProgramData\Pedalboard3
;   - each user's settings: %APPDATA%\Pedalboard 3 (the app's own; untouched here)
;   - patches: Documents\Pedalboard 3\Patches (the app makes it on first run)
; Unsigned for the beta, so SmartScreen warns on first run; signing is #40.
;
; tools/package.ps1 builds it from the portable folder it has just staged, so the
; installer and the zip hold the same files:
;   ISCC /DAppVersion=<version> /DNumericVersion=<x.y.z> /DStageDir=<portable folder>
;        /DIconFile=<icon.ico> /DOutputDir=<dir> installer\windows\Pedalboard3.iss

#ifndef AppVersion
  #error Build this with tools/package.ps1, which passes the version and the folders.
#endif

[Setup]
; Never change the AppId: it's how an upgrade finds the copy already installed.
AppId={{F1531B00-0449-4E27-864C-FFB0A8F25C99}
AppName=Pedalboard 3
AppVersion={#AppVersion}
AppVerName=Pedalboard 3 {#AppVersion}
; The same company name the exe carries (COMPANY_NAME in app-v3/src/CMakeLists.txt).
AppPublisher=AquaOctet
AppPublisherURL=https://gitlab.com/void-realm-solutions/pedalboard-3
VersionInfoVersion={#NumericVersion}
DefaultDirName={autopf}\Pedalboard 3
DefaultGroupName=Pedalboard 3
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Pedalboard3-{#AppVersion}-windows-x64-setup
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\Pedalboard 3.exe
UninstallDisplayName=Pedalboard 3
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; A running copy is closed (the user is asked first) before its files are replaced.
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Dirs]
; Shared data for every user of this PC (factory presets, the default blacklist).
Name: "{commonappdata}\Pedalboard3"

[Files]
; The portable folder: the app, the scanner helper, licences\ and the release notes.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Pedalboard 3"; Filename: "{app}\Pedalboard 3.exe"
Name: "{autodesktop}\Pedalboard 3"; Filename: "{app}\Pedalboard 3.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Pedalboard 3.exe"; Description: "{cm:LaunchProgram,Pedalboard 3}"; Flags: nowait postinstall skipifsilent

; Uninstalling removes only what was installed. Each user's settings
; (%APPDATA%\Pedalboard 3), patches and recordings (Documents\Pedalboard 3) stay.
