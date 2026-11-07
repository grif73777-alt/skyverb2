; skyverb installer (Inno Setup 6). Built by GitHub Actions, see .github/workflows/build.yml
; Expects the plugin bundle at ..\dist\skyverb.vst3

#define MyAppName      "skyverb"
#ifndef MyAppVersion
  #define MyAppVersion "1.1.0"
#endif
#define MyPublisher    "tripleskyluv corp"

[Setup]
AppId={{6B1F3C2E-5D7A-4E58-9A0B-3C8E1D27F4A1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyPublisher}
VersionInfoVersion={#MyAppVersion}
; the VST3 bundle goes to the standard system folder that every DAW scans
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
CreateAppDir=yes
UsePreviousAppDir=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\setup-out
OutputBaseFilename=skyverb-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} (VST3)
DisableWelcomePage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[InstallDelete]
; clean up the old build that was called "Skyverb.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Skyverb.vst3"

[Files]
Source: "..\dist\skyverb.vst3\*"; DestDir: "{commoncf64}\VST3\skyverb.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion

[Messages]
FinishedLabel=skyverb has been installed to the VST3 folder.%n%nIn FL Studio: Options > Manage plugins > Find installed plugins, then add skyverb to a mixer insert.
