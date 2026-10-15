; Builds Patternspace-<version>-Windows-Setup.exe with Inno Setup 6:
;
;   iscc /DAppVersion=0.3.0 /DArtefacts=build\Patternspace_artefacts\Release /DOutputDir=dist packaging\windows\Patternspace.iss
;
; It installs the standalone app into Program Files and the VST3 and CLAP plug-ins into the
; standard Common Files folders, each a component the user can untick. The installer is unsigned
; unless ISCC is given a SignTool command (/SSigntool=... and /DSigned=1), as the CI workflow does
; when the repository has a certificate.

#ifndef AppVersion
  #error Pass /DAppVersion=<version>
#endif
#ifndef Artefacts
  #error Pass /DArtefacts=<path to build\Patternspace_artefacts\Release>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{6E2B9C4A-3F1D-4B8E-9A57-5C0D2E7F1A93}
AppName=Patternspace
AppVersion={#AppVersion}
AppVerName=Patternspace {#AppVersion}
AppPublisher=Patternspace
AppPublisherURL=https://github.com/thomas-nelson23/Patternspace
AppUpdatesURL=https://github.com/thomas-nelson23/Patternspace/releases
DefaultDirName={autopf}\Patternspace
DefaultGroupName=Patternspace
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Patternspace.exe
OutputDir={#OutputDir}
OutputBaseFilename=Patternspace-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; The plug-ins go in Common Files, which needs admin rights.
PrivilegesRequired=admin
; Closes a running Patternspace app before replacing it.
CloseApplications=yes
RestartApplications=no
#ifdef Signed
SignTool=Signtool
SignedUninstaller=yes
#endif

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "app"; Description: "Patternspace app"; Types: full custom
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
Name: "clap"; Description: "CLAP plug-in"; Types: full custom

[Files]
Source: "{#Artefacts}\Standalone\Patternspace.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#Artefacts}\VST3\Patternspace.vst3\*"; DestDir: "{commoncf64}\VST3\Patternspace.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\CLAP\Patternspace.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Patternspace"; Filename: "{app}\Patternspace.exe"; Components: app
Name: "{autodesktop}\Patternspace"; Filename: "{app}\Patternspace.exe"; Components: app; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Components: app; Flags: unchecked

[Run]
Filename: "{app}\Patternspace.exe"; Description: "Open Patternspace now"; Components: app; Flags: nowait postinstall skipifsilent
