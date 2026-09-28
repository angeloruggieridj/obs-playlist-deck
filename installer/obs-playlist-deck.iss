; Windows installer for Playlist Deck.
;
; It installs exactly what the zip carries -- the tree `cmake --install` lays
; out -- into the one per-install folder OBS searches on Windows:
;   C:\ProgramData\obs-studio\plugins\obs-playlist-deck\
;     obs-playlist-deck.dll            OBS 33+ layout
;     bin\64bit\obs-playlist-deck.dll  legacy layout, OBS 32 and earlier
;     data\                            shared by both
; OBS 33 loads the first and skips the second as a duplicate; OBS 32 and
; earlier only look at the second. Portable OBS is not covered: use the zip.
;
; Built by CI (see the Windows job in .github/workflows/build_project.yml):
;   ISCC /DAppVersion=1.4.0 /DPkgDir=<install tree> /O<output dir> obs-playlist-deck.iss

#ifndef AppVersion
  #error Pass the plugin version: /DAppVersion=x.y.z
#endif
#ifndef PkgDir
  #error Pass the install tree holding obs-playlist-deck\: /DPkgDir=<dir>
#endif

[Setup]
; Never change the AppId: it is how an upgrade finds the previous install.
AppId={{5F029470-E628-4130-9BF0-C62083EEA147}
AppName=Playlist Deck for OBS
AppVersion={#AppVersion}
AppVerName=Playlist Deck for OBS {#AppVersion}
AppPublisher=Angelo Ruggieri
AppPublisherURL=https://github.com/angeloruggieridj/obs-playlist-deck
AppSupportURL=https://github.com/angeloruggieridj/obs-playlist-deck/issues
AppUpdatesURL=https://github.com/angeloruggieridj/obs-playlist-deck/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={commonappdata}\obs-studio\plugins\obs-playlist-deck
DisableDirPage=yes
DisableProgramGroupPage=yes
; ProgramData subfolders can belong to another account; one clear elevation
; prompt beats an install that fails halfway.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Kept inside the plugin folder so it goes with it; OBS never looks there.
UninstallFilesDir={app}\uninstall
UninstallDisplayName=Playlist Deck for OBS
; OBS is checked for explicitly below; the Restart Manager would only add a
; second, vaguer prompt about the same thing.
CloseApplications=no
; {userappdata} is used on purpose, for the 1.3.2 cleanup task only.
UsedUserAreasWarning=no
OutputBaseFilename=obs-playlist-deck-windows-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
english.ObsRunning=OBS Studio is running. Close it, then choose Retry: a plugin that OBS has loaded cannot be replaced.
italian.ObsRunning=OBS Studio è in esecuzione. Chiudilo, poi scegli Riprova: un plugin caricato da OBS non può essere sostituito.
german.ObsRunning=OBS Studio läuft. Schließen Sie es und wählen Sie dann „Wiederholen“: Ein von OBS geladenes Plugin kann nicht ersetzt werden.
spanish.ObsRunning=OBS Studio está en ejecución. Ciérralo y elige Reintentar: un plugin cargado por OBS no se puede reemplazar.
french.ObsRunning=OBS Studio est en cours d'exécution. Fermez-le, puis choisissez Réessayer : un plugin chargé par OBS ne peut pas être remplacé.
japanese.ObsRunning=OBS Studio が実行中です。終了してから「再試行」を選んでください。OBS が読み込んだプラグインは置き換えられません。
korean.ObsRunning=OBS Studio가 실행 중입니다. 종료한 다음 다시 시도를 선택하세요. OBS가 불러온 플러그인은 교체할 수 없습니다.
brazilianportuguese.ObsRunning=O OBS Studio está em execução. Feche-o e escolha Repetir: um plugin carregado pelo OBS não pode ser substituído.
russian.ObsRunning=OBS Studio запущен. Закройте его и нажмите «Повтор»: загруженный OBS плагин нельзя заменить.
english.CleanLegacy=Remove the copy that older versions placed in %APPDATA% (OBS never loaded it)
italian.CleanLegacy=Rimuovi la copia che le versioni precedenti mettevano in %APPDATA% (OBS non l'ha mai caricata)
german.CleanLegacy=Die Kopie entfernen, die ältere Versionen in %APPDATA% abgelegt haben (OBS hat sie nie geladen)
spanish.CleanLegacy=Eliminar la copia que las versiones anteriores dejaban en %APPDATA% (OBS nunca la cargó)
french.CleanLegacy=Supprimer la copie que les anciennes versions plaçaient dans %APPDATA% (OBS ne l'a jamais chargée)
japanese.CleanLegacy=以前のバージョンが %APPDATA% に置いたコピーを削除する（OBS は一度も読み込んでいません）
korean.CleanLegacy=이전 버전이 %APPDATA%에 둔 사본 제거 (OBS가 불러온 적 없음)
brazilianportuguese.CleanLegacy=Remover a cópia que versões anteriores colocavam em %APPDATA% (o OBS nunca a carregou)
russian.CleanLegacy=Удалить копию, которую старые версии помещали в %APPDATA% (OBS её никогда не загружал)

[Tasks]
Name: "cleanlegacy"; Description: "{cm:CleanLegacy}"; Check: LegacyCopyExists

[InstallDelete]
; Upgrades replace the data folder whole, so a file a newer version dropped
; does not linger from an older one.
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{userappdata}\obs-studio\plugins\obs-playlist-deck"; Tasks: cleanlegacy

[Files]
Source: "{#PkgDir}\obs-playlist-deck\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
; Also what an earlier zip install left behind. Settings and playlists live in
; OBS's plugin_config folder, not here, and are kept.
Type: filesandordirs; Name: "{app}"

[Code]
function IsObsRunning(): Boolean;
var
  Locator, Wmi, Processes: Variant;
begin
  Result := False;
  try
    { Pascal Script cannot call a method on a call's result: one step each. }
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Wmi := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Wmi.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''obs64.exe''');
    Result := Processes.Count > 0;
  except
    { WMI unavailable: do not block the install on a check that cannot run. }
  end;
end;

{ Retry until OBS is closed, or give up. Silent installs get IDCANCEL, so an
  unattended run fails cleanly instead of looping. }
function WaitForObsClosed(): Boolean;
begin
  Result := True;
  while IsObsRunning() do
    if SuppressibleMsgBox(CustomMessage('ObsRunning'), mbError, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
end;

function LegacyCopyExists(): Boolean;
begin
  Result := DirExists(ExpandConstant('{userappdata}\obs-studio\plugins\obs-playlist-deck'));
end;

function InitializeSetup(): Boolean;
begin
  Result := WaitForObsClosed();
end;

function InitializeUninstall(): Boolean;
begin
  Result := WaitForObsClosed();
end;
