{ Included inside [Code]. Keep installation identity and directory in one plan. }
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall';
type
  TInstalledCopy = record
    Id, Name, Version, Directory: String;
    MachineWide: Boolean;
  end;
var
  Copies: array of TInstalledCopy;
  InstallId, InstallName, InstallDirectory, InstallMode: String;
#if Int(UpgradeTestMode)
  TestRegistryRoot: String;
#endif
  ChoicePage: TInputOptionWizardPage;
  CopyPicker: TNewComboBox;
  PlanConfirmed: Boolean;

function SameDirectory(const A, B: String): Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(ExpandFileName(A)),
                        RemoveBackslashUnlessRoot(ExpandFileName(B))) = 0;
end;

function BaseDirectory: String;
begin
  if IsAdminInstallMode then Result := ExpandConstant('{autopf}\{#ProductName}')
  else Result := ExpandConstant('{localappdata}\Programs\{#ProductName}');
end;

function GetInstallationId(Param: String): String;
begin
  Result := InstallId;
end;

function GetInstallationName(Param: String): String;
begin
  Result := InstallName;
  if Result = '' then Result := '{#ProductName}';
end;

function GetInstallDirectory(Param: String): String;
begin
  Result := InstallDirectory;
  if Result = '' then Result := BaseDirectory;
end;

function GetUninstallName(Param: String): String;
begin
  Result := GetInstallationName('') + ' {#AppVersion}';
end;

procedure AddCopy(const Id, Name, Version, Directory: String; MachineWide: Boolean);
var I, N: Integer;
begin
  if (Directory = '') or not FileExists(AddBackslash(Directory) + 'BiliWave.exe') then Exit;
  for I := 0 to GetArrayLength(Copies) - 1 do
    if SameDirectory(Copies[I].Directory, Directory) then Exit;
  N := GetArrayLength(Copies);
  SetArrayLength(Copies, N + 1);
  Copies[N].Id := Id;
  Copies[N].Name := Name;
  Copies[N].Version := Version;
  Copies[N].Directory := RemoveBackslashUnlessRoot(ExpandFileName(Directory));
  Copies[N].MachineWide := MachineWide;
end;

procedure ScanRegistry(Root: Integer; const Parent: String; MachineWide: Boolean);
var Keys: TArrayOfString; I: Integer; Key, Id, Name, Version, Directory: String;
begin
  if not RegGetSubkeyNames(Root, Parent, Keys) then Exit;
  for I := 0 to GetArrayLength(Keys) - 1 do begin
    Key := Keys[I];
    if (CompareText(Key, '{#ProductId}_is1') <> 0) and
       (Pos(Lowercase('{#ProductId}-side-'), Lowercase(Key)) <> 1) then Continue;
    if CompareText(Copy(Key, Length(Key) - 3, 4), '_is1') <> 0 then Continue;
    Id := Copy(Key, 1, Length(Key) - 4);
    Key := Parent + '\' + Key;
    Directory := ''; Version := ''; Name := '';
    if not RegQueryStringValue(Root, Key, 'InstallLocation', Directory) then
      RegQueryStringValue(Root, Key, 'Inno Setup: App Path', Directory);
    RegQueryStringValue(Root, Key, 'DisplayVersion', Version);
    if not RegQueryStringValue(Root, Key, 'BiliWaveInstanceName', Name) then Name := '{#ProductName}';
    AddCopy(Id, Name, Version, Directory, MachineWide);
  end;
end;

procedure DiscoverCopies;
begin
  SetArrayLength(Copies, 0);
#if Int(UpgradeTestMode)
  ScanRegistry(HKCU, TestRegistryRoot, False);
#else
  if IsVerificationInstall then Exit;
  ScanRegistry(HKCU64, UninstallKey, False);
  ScanRegistry(HKCU32, UninstallKey, False);
  ScanRegistry(HKLM64, UninstallKey, True);
  ScanRegistry(HKLM32, UninstallKey, True);
#endif
end;

function FindCopy(const Directory: String): Integer;
var I: Integer;
begin
  Result := -1;
  for I := 0 to GetArrayLength(Copies) - 1 do
    if SameDirectory(Copies[I].Directory, Directory) then begin Result := I; Exit; end;
end;

function IsDifferentEdition(const Directory: String): Boolean;
begin
  Result := False;
  if (FindCopy(Directory) >= 0) or not FileExists(AddBackslash(Directory) + 'BiliWave.exe') then Exit;
  { Standard portable copies can be distinguished by their screen OCR worker. }
#if Int(LiteEdition)
  Result := FileExists(AddBackslash(Directory) + 'workers\lyric_worker.py');
#else
  Result := not FileExists(AddBackslash(Directory) + 'workers\lyric_worker.py');
#endif
end;

function DirectoryHasContent(const Directory: String): Boolean;
var Found: TFindRec;
begin
  Result := False;
  if FindFirst(AddBackslash(Directory) + '*', Found) then begin
    try
      repeat
        if (Found.Name <> '.') and (Found.Name <> '..') then begin Result := True; Exit; end;
      until not FindNext(Found);
    finally
      FindClose(Found);
    end;
  end;
end;

function IdentityExists(const Id: String): Boolean;
var I: Integer; Key: String;
begin
  Result := True;
  for I := 0 to GetArrayLength(Copies) - 1 do
    if CompareText(Copies[I].Id, Id) = 0 then Exit;
#if Int(UpgradeTestMode)
  Key := TestRegistryRoot + '\' + Id + '_is1';
  Result := RegKeyExists(HKCU, Key);
#else
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\' + Id + '_is1';
  Result := RegKeyExists(HKCU64, Key) or RegKeyExists(HKCU32, Key) or
            RegKeyExists(HKLM64, Key) or RegKeyExists(HKLM32, Key);
#endif
end;

procedure SelectSeparate;
var N: Integer; Parent, Suffix: String;
begin
  Parent := ExtractFileDir(BaseDirectory);
#if Int(UpgradeTestMode)
  Parent := ExpandConstant('{param:TESTSIDEBASE|}');
#endif
  N := 0;
  repeat
    N := N + 1;
    Suffix := '{#AppVersion}-' + IntToStr(N);
    InstallId := '{#ProductId}-side-' + Suffix;
    InstallName := '{#ProductName} ' + Suffix;
    InstallDirectory := AddBackslash(Parent) + InstallName;
  until not IdentityExists(InstallId) and not DirExists(InstallDirectory);
  InstallMode := 'separate';
  PlanConfirmed := True;
end;

function SelectUpgrade(Index: Integer): Boolean;
begin
  Result := False;
  if (Index < 0) or (Index >= GetArrayLength(Copies)) then Exit;
  if Copies[Index].MachineWide and not IsAdminInstallMode then begin
    SuppressibleMsgBox('这个版本安装在系统级目录。请以管理员身份使用 /ALLUSERS 重新运行安装程序，或选择单独安装。',
                      mbError, MB_OK, IDOK);
    Exit;
  end;
  if not Copies[Index].MachineWide and IsAdminInstallMode then begin
    SuppressibleMsgBox('所选版本属于当前用户，请不带 /ALLUSERS 重新运行，或选择单独安装。', mbError, MB_OK, IDOK);
    Exit;
  end;
  InstallId := Copies[Index].Id;
  InstallName := Copies[Index].Name;
  InstallDirectory := Copies[Index].Directory;
  InstallMode := 'upgrade';
  PlanConfirmed := True;
  Result := True;
end;

function InitializeSetup: Boolean;
var Requested, Target: String; Index: Integer;
begin
  Result := False;
  InstallId := '{#ProductId}';
  InstallName := '{#ProductName}';
  InstallDirectory := BaseDirectory;
  InstallMode := 'fresh';
  Target := ExpandConstant('{param:DIR|}');
  if IsVerificationInstall and (Target = '') then begin
    Log('Verification requires an explicit destination directory.'); Exit;
  end;
#if Int(UpgradeTestMode)
  TestRegistryRoot := ExpandConstant('{param:TESTREGISTRYROOT|}');
  if Pos('Software\BiliWave\InstallerTests\', TestRegistryRoot) <> 1 then Exit;
  if ExpandConstant('{param:TESTSIDEBASE|}') = '' then Exit;
#endif
  DiscoverCopies;
#if !Int(UpgradeTestMode)
  if not IsVerificationInstall and not IsDifferentEdition(BaseDirectory) then
    AddCopy('{#ProductId}', '{#ProductName}', '未注册版本', BaseDirectory, IsAdminInstallMode);
#endif
  Requested := Lowercase(ExpandConstant('{param:INSTALLMODE|}'));
  if (Requested <> '') and (Requested <> 'upgrade') and (Requested <> 'separate') then begin
    Log('Invalid /INSTALLMODE. Use upgrade or separate.'); Exit;
  end;
  if Requested = 'separate' then SelectSeparate
  else if Requested = 'upgrade' then begin
    Target := ExpandConstant('{param:UPGRADETARGET|}');
    if Target <> '' then Index := FindCopy(Target)
    else if GetArrayLength(Copies) = 1 then Index := 0
    else Index := -1;
    if not SelectUpgrade(Index) then begin Log('Upgrade requires an unambiguous detected /UPGRADETARGET.'); Exit; end;
  end else begin
    PlanConfirmed := GetArrayLength(Copies) = 0;
    if WizardSilent and not PlanConfirmed then begin
      Log('Existing installation detected. Explicit /INSTALLMODE=upgrade or separate is required.'); Exit;
    end;
  end;
  if (Requested = 'separate') and (Target <> '') then InstallDirectory := Target;
  Result := True;
end;

procedure InitializeWizard;
var I: Integer; CopyLabel: TNewStaticText;
begin
  if GetArrayLength(Copies) = 0 then Exit;
  ChoicePage := CreateInputOptionPage(wpWelcome, '检测到已有版本', '覆盖升级，还是单独安装？',
    '覆盖升级保留原目录的乐库和设置；单独安装使用独立目录、快捷方式和卸载项。请选择后继续。', True, False);
  ChoicePage.Add('覆盖升级所选版本');
  ChoicePage.Add('单独安装，保留已有版本');
  ChoicePage.CheckListBox.Height := ScaleY(60);
  CopyLabel := TNewStaticText.Create(ChoicePage);
  CopyLabel.Parent := ChoicePage.Surface;
  CopyLabel.Caption := '覆盖时选择已有版本（单独安装时忽略）：';
  CopyLabel.SetBounds(0, ChoicePage.CheckListBox.Top + ChoicePage.CheckListBox.Height + ScaleY(12),
                      ChoicePage.SurfaceWidth, ScaleY(20));
  CopyPicker := TNewComboBox.Create(ChoicePage);
  CopyPicker.Parent := ChoicePage.Surface;
  CopyPicker.Style := csDropDownList;
  CopyPicker.SetBounds(0, CopyLabel.Top + CopyLabel.Height + ScaleY(6), ChoicePage.SurfaceWidth, ScaleY(25));
  for I := 0 to GetArrayLength(Copies) - 1 do
    CopyPicker.Items.Add(Copies[I].Name + ' ' + Copies[I].Version + ' — ' + Copies[I].Directory);
  CopyPicker.ItemIndex := 0;
  if PlanConfirmed then begin
    ChoicePage.Values[0] := InstallMode = 'upgrade';
    ChoicePage.Values[1] := InstallMode = 'separate';
    if InstallMode = 'upgrade' then CopyPicker.ItemIndex := FindCopy(InstallDirectory);
  end else begin
    ChoicePage.Values[0] := False;
    ChoicePage.Values[1] := False;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var Target: String; Index: Integer;
begin
  Result := False;
  if Assigned(ChoicePage) and (CurPageID = ChoicePage.ID) then begin
    if ChoicePage.Values[0] then begin if not SelectUpgrade(CopyPicker.ItemIndex) then Exit; end
    else if ChoicePage.Values[1] then begin
      if InstallMode <> 'separate' then SelectSeparate;
    end else begin
      SuppressibleMsgBox('请选择覆盖升级或单独安装。', mbInformation, MB_OK, IDOK); Exit;
    end;
    WizardForm.DirEdit.Text := InstallDirectory;
  end;
  if CurPageID = wpSelectDir then begin
    Target := WizardDirValue;
    if IsDifferentEdition(Target) then begin
      SuppressibleMsgBox('所选目录包含另一种版本（加装版 / 简装版），请选择独立目录，避免互相覆盖。', mbInformation, MB_OK, IDOK); Exit;
    end;
    if (InstallMode = 'upgrade') and not SameDirectory(Target, InstallDirectory) then begin
      SuppressibleMsgBox('覆盖升级必须使用所选版本原有目录；如需其他目录，请返回选择单独安装。', mbInformation, MB_OK, IDOK); Exit;
    end;
    if (InstallMode = 'separate') and DirectoryHasContent(Target) then begin
      SuppressibleMsgBox('单独安装需要一个新目录或空目录，不能复用已有文件和乐库。', mbInformation, MB_OK, IDOK); Exit;
    end;
    if (InstallMode <> 'upgrade') and FileExists(AddBackslash(Target) + 'BiliWave.exe') then begin
      if InstallMode = 'separate' then begin
        SuppressibleMsgBox('这个目录已有 BiliWave。单独安装请选择一个新目录，或返回选择覆盖升级。', mbInformation, MB_OK, IDOK); Exit;
      end;
      if WizardSilent then begin Log('Destination contains BiliWave; explicit upgrade choice required.'); Exit; end;
      Index := FindCopy(Target);
      if Index < 0 then begin
        AddCopy('{#ProductId}', '{#ProductName}', '未注册版本', Target, False);
        Index := FindCopy(Target);
      end;
      case TaskDialogMsgBox('所选目录已有 BiliWave', Target, mbConfirmation, MB_YESNOCANCEL, ['覆盖升级'#13#10'保留原目录的乐库和设置', '单独安装'#13#10'改用新的独立目录', '取消'], 0) of
        IDYES: begin if not SelectUpgrade(Index) then Exit; end;
        IDNO: begin SelectSeparate; WizardForm.DirEdit.Text := InstallDirectory; Exit; end;
      else Exit;
      end;
    end;
    InstallDirectory := WizardDirValue;
  end;
  Result := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not PlanConfirmed then Result := '必须先选择覆盖升级或单独安装。';
  if (InstallMode = 'separate') and DirectoryHasContent(WizardDirValue) then
    Result := '单独安装的目录不是空目录，请返回选择新目录。';
  if IsDifferentEdition(WizardDirValue) then Result := '所选目录包含另一种版本，请返回选择独立目录。';
  if (InstallMode = 'upgrade') and not SameDirectory(WizardDirValue, InstallDirectory) then
    Result := '升级目录与所选版本不一致，请重新选择。';
#if Int(UpgradeTestMode)
  if Result = '' then begin
    SetIniString('Plan', 'Mode', InstallMode, ExpandConstant('{param:TESTPLAN}'));
    SetIniString('Plan', 'AppId', InstallId, ExpandConstant('{param:TESTPLAN}'));
    SetIniString('Plan', 'Name', InstallName, ExpandConstant('{param:TESTPLAN}'));
    SetIniString('Plan', 'Directory', WizardDirValue, ExpandConstant('{param:TESTPLAN}'));
    SetIniString('Plan', 'Detected', IntToStr(GetArrayLength(Copies)), ExpandConstant('{param:TESTPLAN}'));
    if Assigned(ChoicePage) then begin
      SetIniString('Plan', 'ChoiceBottom', IntToStr(ChoicePage.CheckListBox.Top + ChoicePage.CheckListBox.Height), ExpandConstant('{param:TESTPLAN}'));
      SetIniString('Plan', 'PickerTop', IntToStr(CopyPicker.Top), ExpandConstant('{param:TESTPLAN}'));
      SetIniString('Plan', 'PickerBottom', IntToStr(CopyPicker.Top + CopyPicker.Height), ExpandConstant('{param:TESTPLAN}'));
      SetIniString('Plan', 'SurfaceHeight', IntToStr(ChoicePage.SurfaceHeight), ExpandConstant('{param:TESTPLAN}'));
    end;
  end;
#endif
end;
