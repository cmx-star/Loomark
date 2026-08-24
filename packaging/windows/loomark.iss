#define MyAppName "Loomark"
#ifndef MyAppVersion
#define MyAppVersion "0.1.0"
#endif
#ifndef SourceDir
#define SourceDir "..\..\package"
#endif
#ifndef OutputDir
#define OutputDir "."
#endif

[Setup]
AppId={{8EA56082-EE36-43D7-A7F2-20672755C9BD}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Loomark
DefaultDirName={localappdata}\Programs\Loomark
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=loomark-windows-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\bin\Loomark.exe

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\bin\Loomark.exe"; WorkingDir: "{app}\bin"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\bin\Loomark.exe"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\Loomark.exe"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
