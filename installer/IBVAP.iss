; IBVAP installer script (Inno Setup). Packs everything the build puts in build\bin,
; including the React frontend folder and the phone page.

[Setup]
AppName=IBVAP
AppVersion=0.1.0
AppPublisher=Coding Leyaks
DefaultDirName={autopf}\IBVAP
DefaultGroupName=IBVAP
OutputDir=..\installer_output
OutputBaseFilename=IBVAP_Setup_2
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest

[Files]
Source: "..\build\bin\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\models\*"; DestDir: "{app}\models"; Flags: recursesubdirs createallsubdirs ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\IBVAP"; Filename: "{app}\IBVAP.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\IBVAP"; Filename: "{app}\IBVAP.exe"; WorkingDir: "{app}"

[Registry]
Root: HKCU; Subkey: "Software\Classes\ibvap"; ValueType: string; ValueData: "URL:IBVAP"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\ibvap"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\ibvap\DefaultIcon"; ValueType: string; ValueData: "{app}\IBVAP.exe,0"
Root: HKCU; Subkey: "Software\Classes\ibvap\shell\open\command"; ValueType: string; ValueData: """{app}\IBVAP.exe"""

[Run]
Filename: "{app}\IBVAP.exe"; Description: "Launch IBVAP"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
