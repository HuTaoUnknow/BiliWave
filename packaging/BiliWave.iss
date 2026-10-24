#ifndef AppVersion
  #define AppVersion "1.10.0"
#endif
#ifndef PayloadDir
  #define PayloadDir "..\release"
#endif
#ifndef PackageOutputDir
  #define PackageOutputDir "..\dist"
#endif
#ifndef LiteEdition
  #define LiteEdition 0
#endif
#if Int(LiteEdition)
  #define ProductName "BiliWave Lite"
  #define ProductId "{{EE37A2DA-4D0B-4BCD-97E3-3B3B6141F838}"
  #define PackageName "BiliWave-Lite-" + AppVersion + "-Windows-x64-Setup"
#else
  #define ProductName "BiliWave"
  #define ProductId "{{B1A2A5B4-8F18-449F-8E87-DCB27977DDE2}"
  #define PackageName "BiliWave-" + AppVersion + "-Windows-x64-Setup"
#endif

[Setup]
AppId={#ProductId}
AppName={#ProductName}
AppVersion={#AppVersion}
AppVerName={#ProductName} {#AppVersion}
AppPublisher=BiliWave
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#ProductName} 安装程序
VersionInfoProductName={#ProductName}
DefaultDirName={localappdata}\Programs\{#ProductName}
DefaultGroupName={#ProductName}
DisableProgramGroupPage=yes
DisableDirPage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
SetupLogging=yes
UninstallDisplayIcon={app}\BiliWave.exe
CreateUninstallRegKey=not IsVerificationInstall
OutputDir={#PackageOutputDir}
OutputBaseFilename={#PackageName}
Compression=lzma2/normal
SolidCompression=yes
LZMANumBlockThreads=4
LZMAUseSeparateProcess=yes
DiskSpanning=no
CloseApplications=yes
RestartApplications=no
SetupMutex=BiliWaveInstaller

[Languages]
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "快捷方式："; Check: not IsVerificationInstall

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Excludes: "\data,\data\*,*.lib,*.a,*.pdb,BiliWave-before-*.exe,__pycache__,*.pyc"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\{#ProductName}\{#ProductName}"; Filename: "{app}\BiliWave.exe"; WorkingDir: "{app}"; Check: not IsVerificationInstall
Name: "{userprograms}\{#ProductName}\卸载 {#ProductName}"; Filename: "{uninstallexe}"; Check: not IsVerificationInstall
Name: "{userdesktop}\{#ProductName}"; Filename: "{app}\BiliWave.exe"; WorkingDir: "{app}"; Tasks: desktopicon; Check: not IsVerificationInstall

[Run]
Filename: "{app}\BiliWave.exe"; Description: "运行 {#ProductName}"; Flags: nowait postinstall skipifsilent; Check: not IsVerificationInstall

[Code]
function IsVerificationInstall: Boolean;
begin
  { The build check uses a workspace directory without touching real shortcuts
    or the installed application's Add/Remove Programs registration. }
  Result := ExpandConstant('{param:VERIFYINSTALL|0}') = '1';
end;

function InitializeSetup: Boolean;
var
  Target: String;
begin
  Result := True;
  if IsVerificationInstall then begin
    Target := ExpandConstant('{param:DIR|}');
    if Target = '' then begin
      Log('Verification requires an explicit destination directory.');
      Result := False;
    end;
  end;
end;
