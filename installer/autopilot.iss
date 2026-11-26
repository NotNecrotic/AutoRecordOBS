; Inno Setup script for the Autopilot OBS Studio plugin.
;
; Build the plugin first (cmake --build --preset windows-x64 and
; cmake --install build_x64 --prefix release/<config> --config <config>), then compile this script:
;   iscc installer\autopilot.iss
; CI passes /DMyAppVersion=<version> and /DBuildConfig=<config>.

#define MyAppName "Autopilot"
; Module name: the plugin's folder and DLL name inside OBS.
#define PluginId "autopilot"
#ifndef MyAppVersion
  #define MyAppVersion "3.0.0"
#endif
#ifndef BuildConfig
  #define BuildConfig "RelWithDebInfo"
#endif
#ifndef PluginsRoot
  ; Overridable so the installer can be tested against a scratch folder.
  #define PluginsRoot "{commonappdata}\obs-studio\plugins"
#endif
#ifndef AppIdGuid
  #define AppIdGuid "{{4E0C7B0A-6D0F-4D55-9C6E-2B8C1C3A9F31}"
#endif
#ifndef Privileges
  #define Privileges "admin"
#endif
#define MyAppPublisher "Necrotic"
#define MyAppURL "https://github.com/NotNecrotic/obs-autopilot"
; AppId of the v2 tray application (AutoRecordOBS), used to offer removing it.
#define LegacyAppId "{9C6C3CB0-C936-46F9-9563-CF44EE03BD1A}"

[Setup]
AppId={#AppIdGuid}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
; OBS Studio 28+ loads plugins from %ProgramData%\obs-studio\plugins\<name>\.
DefaultDirName={#PluginsRoot}\{#PluginId}
; OBS only loads a plugin whose folder name matches its DLL name, so never reuse an
; older install's folder (pre-release builds lived in plugins\AutoRecordOBS).
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallDisplayName={#MyAppName} for OBS Studio
UninstallDisplayIcon={uninstallexe}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired={#Privileges}
OutputDir=..\release
OutputBaseFilename={#PluginId}-{#MyAppVersion}-windows-x64-Installer
LicenseFile=..\LICENSE
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "removelegacy"; Description: "Uninstall AutoRecordOBS v2 (the old tray app). Its game list is imported automatically."; Check: LegacyInstalled

[InstallDelete]
; Pre-release builds of this plugin were named AutoRecordOBS; two copies would both control recording.
Type: filesandordirs; Name: "{#PluginsRoot}\AutoRecordOBS"

[Files]
Source: "..\release\{#BuildConfig}\{#PluginId}\bin\64bit\{#PluginId}.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "..\release\{#BuildConfig}\{#PluginId}\bin\64bit\{#PluginId}.pdb"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\release\{#BuildConfig}\{#PluginId}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
function LegacyUninstallString(): String;
var
  Key: String;
begin
  Result := '';
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#LegacyAppId}_is1';
  if not RegQueryStringValue(HKCU, Key, 'UninstallString', Result) then
    if not RegQueryStringValue(HKLM64, Key, 'UninstallString', Result) then
      RegQueryStringValue(HKLM32, Key, 'UninstallString', Result);
  Result := RemoveQuotes(Result);
end;

function LegacyInstalled(): Boolean;
begin
  Result := LegacyUninstallString() <> '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Uninstaller: String;
  ResultCode: Integer;
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('removelegacy') then
  begin
    Uninstaller := LegacyUninstallString();
    if (Uninstaller <> '') and FileExists(Uninstaller) then
    begin
      { v2 runs in the tray; close it so its files can be removed. }
      Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM AutoRecordOBS.exe /F', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      Exec(Uninstaller, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    end;
    { v2's "Start with Windows" option wrote this value; the exe it points to is gone now. }
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'AutoRecordOBS');
  end;
end;
