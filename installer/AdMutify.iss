; AdMutify installer (Inno Setup 6). Built by build.bat with /DAppVersion=<version> /DCopyrightYear=<year>.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef CopyrightYear
  #define CopyrightYear "2026"
#endif

[Setup]
AppId={{AAC15DA2-9943-4B71-866E-5007028EA1A6}
AppName=AdMutify
AppVersion={#AppVersion}
AppVerName=AdMutify {#AppVersion}
AppPublisher=BGCoding
AppCopyright=© {#CopyrightYear} BGCoding
VersionInfoCompany=BGCoding
VersionInfoDescription=AdMutify Setup
VersionInfoProductName=AdMutify
VersionInfoVersion={#AppVersion}
DefaultDirName={code:GetDefaultDir}
DefaultGroupName=AdMutify
DisableProgramGroupPage=auto
AllowNoIcons=yes
DisableWelcomePage=no
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
UsedUserAreasWarning=no
ShowLanguageDialog=no
LanguageDetectionMethod=uilanguage
Compression=lzma2/max
SolidCompression=yes
MinVersion=10.0.19045
UninstallDisplayIcon={app}\AdMutify.exe
SetupIconFile=..\assets\app.ico
OutputDir=..\dist
OutputBaseFilename=AdMutify-Setup-{#AppVersion}
AppMutex=Local\AdMutify.SingleInstance
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
Uninstallable=not IsPortableMode
CreateUninstallRegKey=not IsPortableMode

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"

[CustomMessages]
english.DowngradeBlocked=A newer version of AdMutify (%1) is already installed. This setup would install version %2, so it was stopped.
german.DowngradeBlocked=Eine neuere Version von AdMutify (%1) ist bereits installiert. Dieses Setup würde Version %2 installieren und wurde deshalb beendet.
spanish.DowngradeBlocked=Ya hay instalada una versión más reciente de AdMutify (%1). Esta instalación instalaría la versión %2, por lo que se detuvo.
brazilianportuguese.DowngradeBlocked=Uma versão mais recente do AdMutify (%1) já está instalada. Esta instalação instalaria a versão %2, por isso foi interrompida.
italian.DowngradeBlocked=È già installata una versione più recente di AdMutify (%1). Questa installazione installerebbe la versione %2, quindi è stata interrotta.
dutch.DowngradeBlocked=Er is al een nieuwere versie van AdMutify (%1) geïnstalleerd. Met deze installatie zou versie %2 worden geïnstalleerd, daarom is deze gestopt.
polish.DowngradeBlocked=Nowsza wersja AdMutify (%1) jest już zainstalowana. Ten instalator zainstalowałby wersję %2, dlatego został zatrzymany.
turkish.DowngradeBlocked=AdMutify'ın daha yeni bir sürümü (%1) zaten yüklü. Bu kurulum %2 sürümünü yükleyeceği için durduruldu.
japanese.DowngradeBlocked=新しいバージョンの AdMutify (%1) が既にインストールされています。このセットアップではバージョン %2 がインストールされるため、中止されました。
chinesesimplified.DowngradeBlocked=已安装较新版本的 AdMutify (%1)。此安装程序将安装版本 %2，因此已停止。
english.PortableCheck=Portable installation
german.PortableCheck=Portable Installation
spanish.PortableCheck=Instalación portátil
brazilianportuguese.PortableCheck=Instalação portátil
italian.PortableCheck=Installazione portatile
dutch.PortableCheck=Draagbare installatie
polish.PortableCheck=Instalacja przenośna
turkish.PortableCheck=Taşınabilir kurulum
japanese.PortableCheck=ポータブル インストール
chinesesimplified.PortableCheck=便携安装
english.PortableHint=Keeps settings next to the program. No Start menu entry, no uninstaller.
german.PortableHint=Speichert die Einstellungen neben dem Programm. Kein Startmenü-Eintrag, kein Deinstallationsprogramm.
spanish.PortableHint=Guarda la configuración junto al programa. Sin entrada en el menú Inicio ni desinstalador.
brazilianportuguese.PortableHint=Mantém as configurações junto ao programa. Sem entrada no menu Iniciar e sem desinstalador.
italian.PortableHint=Salva le impostazioni accanto al programma. Nessuna voce nel menu Start e nessun programma di disinstallazione.
dutch.PortableHint=Slaat instellingen naast het programma op. Geen vermelding in het Startmenu en geen verwijderprogramma.
polish.PortableHint=Przechowuje ustawienia obok programu. Bez wpisu w menu Start i bez deinstalatora.
turkish.PortableHint=Ayarları programın yanında tutar. Başlat menüsü girdisi ve kaldırıcı yoktur.
japanese.PortableHint=設定をプログラムと同じ場所に保存します。スタート メニュー項目とアンインストーラーは作成されません。
chinesesimplified.PortableHint=将设置保存在程序旁边。不创建开始菜单项，也不提供卸载程序。
english.CloseFailed=AdMutify is still running and could not be closed. Close it from its tray icon and try again.
german.CloseFailed=AdMutify läuft noch und konnte nicht geschlossen werden. Beende es über das Tray-Icon und versuche es erneut.
spanish.CloseFailed=AdMutify sigue en ejecución y no se pudo cerrar. Ciérralo desde su icono del área de notificación e inténtalo de nuevo.
brazilianportuguese.CloseFailed=O AdMutify ainda está em execução e não pôde ser fechado. Feche-o pelo ícone na área de notificação e tente novamente.
italian.CloseFailed=AdMutify è ancora in esecuzione e non è stato possibile chiuderlo. Chiudilo dalla sua icona nell'area di notifica e riprova.
dutch.CloseFailed=AdMutify wordt nog uitgevoerd en kan niet worden afgesloten. Sluit het via het pictogram in het systeemvak en probeer het opnieuw.
polish.CloseFailed=AdMutify nadal działa i nie można go zamknąć. Zamknij go za pomocą ikony w obszarze powiadomień i spróbuj ponownie.
turkish.CloseFailed=AdMutify hâlâ çalışıyor ve kapatılamadı. Bildirim alanı simgesinden kapatın ve yeniden deneyin.
japanese.CloseFailed=AdMutify はまだ実行中で、終了できませんでした。通知領域のアイコンから終了して、もう一度お試しください。
chinesesimplified.CloseFailed=AdMutify 仍在运行，无法关闭。请通过通知区域图标将其关闭，然后重试。
english.DeleteDataQuestion=Also delete AdMutify's settings and logs?%n%nFolder: %1
german.DeleteDataQuestion=Auch die Einstellungen und Logs von AdMutify löschen?%n%nOrdner: %1
spanish.DeleteDataQuestion=¿Eliminar también la configuración y los registros de AdMutify?%n%nCarpeta: %1
brazilianportuguese.DeleteDataQuestion=Excluir também as configurações e os logs do AdMutify?%n%nPasta: %1
italian.DeleteDataQuestion=Eliminare anche le impostazioni e i log di AdMutify?%n%nCartella: %1
dutch.DeleteDataQuestion=Ook de instellingen en logboeken van AdMutify verwijderen?%n%nMap: %1
polish.DeleteDataQuestion=Usunąć również ustawienia i dzienniki AdMutify?%n%nFolder: %1
turkish.DeleteDataQuestion=AdMutify ayarları ve günlükleri de silinsin mi?%n%nKlasör: %1
japanese.DeleteDataQuestion=AdMutify の設定とログも削除しますか?%n%nフォルダー: %1
chinesesimplified.DeleteDataQuestion=是否同时删除 AdMutify 的设置和日志？%n%n文件夹: %1
english.PortableFolderNotWritable=Portable installation needs a folder that can be written to without administrator rights. Choose another folder, for example one in your user folder or on a USB drive.
german.PortableFolderNotWritable=Die portable Installation braucht einen Ordner, in den ohne Administratorrechte geschrieben werden kann. Wähle einen anderen Ordner, zum Beispiel in deinem Benutzerordner oder auf einem USB-Stick.
spanish.PortableFolderNotWritable=La instalación portátil necesita una carpeta en la que se pueda escribir sin permisos de administrador. Elige otra carpeta, por ejemplo una de tu carpeta de usuario o de una unidad USB.
brazilianportuguese.PortableFolderNotWritable=A instalação portátil precisa de uma pasta na qual seja possível gravar sem direitos de administrador. Escolha outra pasta, por exemplo na sua pasta de usuário ou em um pen drive.
italian.PortableFolderNotWritable=L'installazione portatile richiede una cartella in cui sia possibile scrivere senza diritti di amministratore. Scegli un'altra cartella, per esempio nella tua cartella utente o su una chiavetta USB.
dutch.PortableFolderNotWritable=Voor de draagbare installatie is een map nodig waarin zonder beheerdersrechten kan worden geschreven. Kies een andere map, bijvoorbeeld in je gebruikersmap of op een USB-stick.
polish.PortableFolderNotWritable=Instalacja przenośna wymaga folderu, do którego można zapisywać bez uprawnień administratora. Wybierz inny folder, na przykład w folderze użytkownika lub na pamięci USB.
turkish.PortableFolderNotWritable=Taşınabilir kurulum, yönetici hakları olmadan yazılabilen bir klasör gerektirir. Başka bir klasör seçin, örneğin kullanıcı klasörünüzde veya bir USB bellekte.
japanese.PortableFolderNotWritable=ポータブル インストールには、管理者権限なしで書き込めるフォルダーが必要です。ユーザー フォルダー内や USB メモリー上など、別のフォルダーを選択してください。
chinesesimplified.PortableFolderNotWritable=便携安装需要一个无需管理员权限即可写入的文件夹。请选择其他文件夹，例如用户文件夹中或 U 盘上的文件夹。

english.OtherModeInstalled=AdMutify is already installed in the other mode (for your account only, or for all users). Install in the existing mode, or uninstall the existing installation first.
german.OtherModeInstalled=AdMutify ist bereits im anderen Modus installiert (nur für dein Konto oder für alle Benutzer). Installiere im vorhandenen Modus oder deinstalliere die vorhandene Installation zuerst.
spanish.OtherModeInstalled=AdMutify ya está instalado en el otro modo (solo para tu cuenta o para todos los usuarios). Instálalo en el modo existente o desinstala antes la instalación existente.
brazilianportuguese.OtherModeInstalled=O AdMutify já está instalado no outro modo (somente para a sua conta ou para todos os usuários). Instale no modo existente ou desinstale antes a instalação existente.
italian.OtherModeInstalled=AdMutify è già installato nell'altra modalità (solo per il tuo account o per tutti gli utenti). Installa nella modalità esistente oppure disinstalla prima l'installazione esistente.
dutch.OtherModeInstalled=AdMutify is al in de andere modus geïnstalleerd (alleen voor je account of voor alle gebruikers). Installeer in de bestaande modus of verwijder eerst de bestaande installatie.
polish.OtherModeInstalled=AdMutify jest już zainstalowany w innym trybie (tylko dla Twojego konta lub dla wszystkich użytkowników). Zainstaluj w istniejącym trybie albo najpierw odinstaluj istniejącą instalację.
turkish.OtherModeInstalled=AdMutify diğer modda zaten yüklü (yalnızca hesabınız için veya tüm kullanıcılar için). Mevcut modda yükleyin ya da önce mevcut kurulumu kaldırın.
japanese.OtherModeInstalled=AdMutify は既に別のモード (自分のアカウントのみ、またはすべてのユーザー) でインストールされています。既存のモードでインストールするか、先に既存のインストールをアンインストールしてください。
chinesesimplified.OtherModeInstalled=AdMutify 已以另一种模式安装（仅当前账户或所有用户）。请使用现有模式安装，或先卸载现有安装。

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Check: not IsPortableMode

[Files]
Source: "..\build\x64\Release\AdMutify.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\packaging\README.txt"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
Type: files; Name: "{app}\portable.txt"; Check: not IsPortableMode

[Icons]
Name: "{group}\AdMutify"; Filename: "{app}\AdMutify.exe"; Check: not IsPortableMode
Name: "{group}\{cm:UninstallProgram,AdMutify}"; Filename: "{uninstallexe}"; Check: not IsPortableMode
Name: "{autodesktop}\AdMutify"; Filename: "{app}\AdMutify.exe"; Tasks: desktopicon; Check: not IsPortableMode

[Run]
Filename: "{app}\AdMutify.exe"; Description: "{cm:LaunchProgram,AdMutify}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
const
  WM_CLOSE = $0010;
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{AAC15DA2-9943-4B71-866E-5007028EA1A6}_is1';
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  StartupApprovedKey = 'Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run';
  AppMutexName = 'Local\AdMutify.SingleInstance';

var
  InstalledVersion: String;
  PortableCheck: TNewCheckBox;
  PortableHint: TNewStaticText;
  DeleteUserData: Boolean;
  WasRunning: Boolean;

function SetForegroundWindow(hWnd: HWND): Boolean; external 'SetForegroundWindow@user32.dll stdcall';
function AllowSetForegroundWindow(dwProcessId: DWORD): Boolean; external 'AllowSetForegroundWindow@user32.dll stdcall';

function IsPortableMode: Boolean;
begin
  Result := ((PortableCheck <> nil) and PortableCheck.Checked) or (ExpandConstant('{param:PORTABLE|0}') = '1');
end;

function GetDefaultDir(Param: String): String;
begin
  if IsPortableMode then
    Result := ExtractFilePath(ExpandConstant('{srcexe}')) + 'AdMutify'
  else
    Result := ExpandConstant('{autopf}\AdMutify');
end;

function CompareVersions(const A, B: String): Integer;
var
  PA, PB: Int64;
begin
  if not StrToVersion(A, PA) then PA := 0;
  if not StrToVersion(B, PB) then PB := 0;
  Result := ComparePackedVersion(PA, PB);
end;

function ReadUninstallValue(const Name: String; var Value: String): Boolean;
begin
  Result := RegQueryStringValue(HKCU, UninstallKey, Name, Value) or
            RegQueryStringValue(HKLM64, UninstallKey, Name, Value);
end;

function CloseRunningApp: Boolean;
var
  Wnd: HWND;
  I: Integer;
begin
  Result := True;
  if not CheckForMutexes(AppMutexName) then Exit;
  Wnd := FindWindowByClassName('AdMutify.Msg');
  if Wnd <> 0 then
    PostMessage(Wnd, WM_CLOSE, 0, 0);
  for I := 1 to 50 do
  begin
    if not CheckForMutexes(AppMutexName) then Break;
    Sleep(100);
  end;
  Result := not CheckForMutexes(AppMutexName);
end;

procedure RaiseWizard;
begin
  AllowSetForegroundWindow($FFFFFFFF);
  SetForegroundWindow(WizardForm.Handle);
  WizardForm.BringToFront;
end;

// True when the same app is registered in the install mode this setup is not running in.
function OtherModeInstalled: Boolean;
begin
  if IsAdminInstallMode then
    Result := RegKeyExists(HKCU, UninstallKey)
  else
    Result := RegKeyExists(HKLM64, UninstallKey);
end;

function InitializeSetup: Boolean;
begin
  Result := True;
  if not IsPortableMode and OtherModeInstalled then
  begin
    MsgBox(CustomMessage('OtherModeInstalled'), mbError, MB_OK);
    Result := False;
    Exit;
  end;
  if ReadUninstallValue('DisplayVersion', InstalledVersion) then
  begin
    if CompareVersions(InstalledVersion, '{#AppVersion}') > 0 then
    begin
      MsgBox(FmtMessage(CustomMessage('DowngradeBlocked'), [InstalledVersion, '{#AppVersion}']), mbError, MB_OK);
      Result := False;
      Exit;
    end;
  end
  else
    InstalledVersion := '';
end;

procedure PortableClick(Sender: TObject);
begin
  if PortableCheck.Checked then
  begin
    WizardForm.DirEdit.Text := ExtractFilePath(ExpandConstant('{srcexe}')) + 'AdMutify';
    WizardForm.NextButton.Caption := SetupMessage(msgButtonInstall);
  end
  else
  begin
    WizardForm.DirEdit.Text := ExpandConstant('{autopf}\AdMutify');
    WizardForm.NextButton.Caption := SetupMessage(msgButtonNext);
  end;
end;

procedure InitializeWizard;
begin
  PortableCheck := TNewCheckBox.Create(WizardForm);
  PortableCheck.Parent := WizardForm.SelectDirPage;
  PortableCheck.Left := 0;
  PortableCheck.Top := WizardForm.DirEdit.Top + WizardForm.DirEdit.Height + ScaleY(14);
  PortableCheck.Width := WizardForm.SelectDirPage.ClientWidth;
  PortableCheck.Caption := CustomMessage('PortableCheck');
  PortableCheck.OnClick := @PortableClick;

  PortableHint := TNewStaticText.Create(WizardForm);
  PortableHint.Parent := WizardForm.SelectDirPage;
  PortableHint.Left := ScaleX(20);
  PortableHint.Top := PortableCheck.Top + PortableCheck.Height + ScaleY(2);
  PortableHint.Width := WizardForm.SelectDirPage.ClientWidth - ScaleX(20);
  PortableHint.AutoSize := False;
  PortableHint.WordWrap := True;
  PortableHint.Height := ScaleY(32);
  PortableHint.Caption := CustomMessage('PortableHint');

  RaiseWizard;
end;

function IsUnderDir(const Path, Dir: String): Boolean;
var
  P, D: String;
begin
  Result := False;
  if Dir = '' then Exit;
  P := Lowercase(AddBackslash(RemoveBackslash(Path)));
  D := Lowercase(AddBackslash(RemoveBackslash(Dir)));
  Result := Copy(P, 1, Length(D)) = D;
end;

// Creates a file in Dir as the user who will run the app: in an all-users (elevated) setup the test file is
// written by the original, unelevated user, so a folder only an administrator can write to is refused.
function CanCreateFileAsRunningUser(const Dir: String): Boolean;
var
  TestFile: String;
  Code: Integer;
begin
  TestFile := AddBackslash(Dir) + 'admutify-write-test.tmp';
  if IsAdminInstallMode then
  begin
    ExecAsOriginalUser(ExpandConstant('{cmd}'), '/C type nul > "' + TestFile + '"', '', SW_HIDE, ewWaitUntilTerminated, Code);
    Result := FileExists(TestFile);
  end
  else
    Result := SaveStringToFile(TestFile, 'x', False);
  if FileExists(TestFile) then DeleteFile(TestFile);
end;

// A portable copy keeps its settings, state file and logs next to the exe, so the folder must be writable
// by the user who runs it: not below Program Files, and a file can be created there.
function PortableFolderUsable(const Dir: String): Boolean;
var
  Created: array of String;
  P, Parent: String;
  I: Integer;
begin
  Result := False;
  if IsUnderDir(Dir, GetEnv('ProgramFiles')) or IsUnderDir(Dir, GetEnv('ProgramFiles(x86)')) or
     IsUnderDir(Dir, GetEnv('ProgramW6432')) then Exit;
  // Folders that do not exist yet are created for the test and removed again, deepest first.
  P := RemoveBackslash(Dir);
  while (P <> '') and not DirExists(P) do
  begin
    SetArrayLength(Created, GetArrayLength(Created) + 1);
    Created[GetArrayLength(Created) - 1] := P;
    Parent := ExtractFileDir(P);
    if Parent = P then Break;
    P := Parent;
  end;
  if (GetArrayLength(Created) = 0) or ForceDirectories(Dir) then
    Result := CanCreateFileAsRunningUser(Dir);
  for I := 0 to GetArrayLength(Created) - 1 do
    RemoveDir(Created[I]);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and IsPortableMode and not PortableFolderUsable(WizardForm.DirEdit.Text) then
  begin
    MsgBox(CustomMessage('PortableFolderNotWritable'), mbError, MB_OK);
    Result := False;
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if IsPortableMode and ((PageID = wpSelectProgramGroup) or (PageID = wpSelectTasks)) then
    Result := True;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectDir then
  begin
    if PortableCheck.Checked then
      WizardForm.NextButton.Caption := SetupMessage(msgButtonInstall)
    else
      WizardForm.NextButton.Caption := SetupMessage(msgButtonNext);
  end;
  RaiseWizard;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  WasRunning := CheckForMutexes(AppMutexName);
  if not CloseRunningApp then
    Result := CustomMessage('CloseFailed');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
begin
  // A silent upgrade has no finish page to launch from, so bring back an instance that was running.
  if (CurStep = ssPostInstall) and WizardSilent and WasRunning then
    ExecAsOriginalUser(ExpandConstant('{app}\AdMutify.exe'), '', '', SW_SHOWNORMAL, ewNoWait, Code);
  if (CurStep = ssPostInstall) and IsPortableMode then
    SaveStringToFile(ExpandConstant('{app}\portable.txt'), 'Settings are stored next to AdMutify.exe.' + #13#10, False);
end;

function InitializeUninstall: Boolean;
begin
  Result := CloseRunningApp;
  if not Result then
  begin
    if not UninstallSilent then
      MsgBox(CustomMessage('CloseFailed'), mbError, MB_OK);
    Exit;
  end;
  DeleteUserData := False;
  if not UninstallSilent then
    // The folder is named so an elevated uninstall shows which account's data it would delete.
    DeleteUserData := MsgBox(FmtMessage(CustomMessage('DeleteDataQuestion'), [ExpandConstant('{userappdata}\AdMutify')]),
                             mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  RunValue, RunName: String;
  I: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    // Only remove the autostart entry when it points at the exe being uninstalled, so a portable copy's
    // entry of the same name survives.
    // A portable installation made by this setup uses its own value name.
    for I := 0 to 1 do
    begin
      if I = 0 then RunName := 'AdMutify' else RunName := 'AdMutify Portable';
      if RegQueryStringValue(HKCU, RunKey, RunName, RunValue) and
         (Pos(Lowercase(ExpandConstant('{app}\AdMutify.exe')), Lowercase(RunValue)) > 0) then
      begin
        RegDeleteValue(HKCU, RunKey, RunName);
        RegDeleteValue(HKCU, StartupApprovedKey, RunName);
      end;
    end;
  end;
  if (CurUninstallStep = usPostUninstall) and DeleteUserData then
  begin
    DelTree(ExpandConstant('{userappdata}\AdMutify'), True, True, True);
    DelTree(ExpandConstant('{localappdata}\AdMutify'), True, True, True);
  end;
end;
