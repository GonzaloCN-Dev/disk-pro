; Instalador de Disk Pro (Inno Setup 6).
; Compilar con: installer\build-installer.ps1

#ifndef AppVersion
  #define AppVersion "0.3.0"
#endif
#define AppName "Disk Pro"
#define AppPublisher "Gonzalo Cruz Núñez"
#define AppExe "DiskPro.exe"

[Setup]
AppId={{3D9B7A14-6E52-4C08-B1F7-A84C02D95E63}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://dev.gonzalocn.com/
AppCopyright=© 2026 {#AppPublisher}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=Instalador de {#AppName}
DefaultDirName={autopf}\Disk Pro
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
SetupIconFile=..\assets\icon.ico
OutputDir=..\dist
OutputBaseFilename=DiskPro-Setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
RestartApplications=no
DisableProgramGroupPage=yes
LicenseFile=LICENSES.txt

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"; Flags: unchecked

[Files]
Source: "payload\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\diskpro-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\diskpro-inventory.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\diskpro-tools.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\diskpro-restore.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\SLINT-LICENSE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Abrir {#AppName}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\{#AppExe}"; Flags: nowait runasoriginaluser; Check: RelaunchRequested

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM {#AppExe}"; Flags: runhidden; RunOnceId: "KillApp"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
function RelaunchRequested: Boolean;
begin
  Result := ExpandConstant('{param:RELAUNCH|0}') = '1';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and (not UninstallSilent()) then
    if MsgBox('¿Quieres borrar también los ajustes de Disk Pro (tema, idioma y densidad)?' + #13#10 +
              '(Disk Pro no modifica tus discos ni guarda datos de ellos.)',
              mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(ExpandConstant('{userappdata}\DiskPro'), True, True, True);
end;



