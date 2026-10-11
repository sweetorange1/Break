#define MyAppName "Break"
#define MyAppVersion "1.0.2"
#define MyAppPublisher "iisaacbeats.cn"
#define MyPluginBundle "Break.vst3"
#define MyPluginBundleAAX "Break.aaxplugin"

; VST3 / AAX 顶层目录（即包含 bundle 的父目录）。
; 默认指向 Visual Studio 的 Release 构建目录；
; build_installer.bat 会用 -DVST3_DIR / -DAAX_DIR 覆盖为实际探测到的路径。
#ifndef VST3_DIR
  #define VST3_DIR "cmake-build-vs\Break_artefacts\Release\VST3"
#endif
#ifndef AAX_DIR
  #define AAX_DIR "cmake-build-vs\Break_artefacts\Release\AAX"
#endif

[Setup]
AppId={{E41D9B3C-7A2F-4C6E-8B15-2D9A3F6C1E84}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={commoncf}\VST3\iisaacbeats.cn
OutputDir=dist
OutputBaseFilename={#MyAppName}_Setup_{#MyAppVersion}_x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
SetupLogging=yes
UsePreviousAppDir=no
DisableProgramGroupPage=yes
DisableDirPage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#VST3_DIR}\{#MyPluginBundle}\*"; DestDir: "{app}\{#MyPluginBundle}"; Flags: ignoreversion recursesubdirs createallsubdirs
; AAX 安装到系统级 Avid 插件目录（Pro Tools 专属，未签名时需开启 AAX Developer Mode 才能加载）
Source: "{#AAX_DIR}\{#MyPluginBundleAAX}\*"; DestDir: "{commoncf}\Avid\Audio\Plug-Ins\{#MyPluginBundleAAX}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
var
  InstallDirWarningShown: Boolean;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  DefaultVst3Path: string;
begin
  Result := True;

  if CurPageID = wpSelectDir then
  begin
    DefaultVst3Path := ExpandConstant('{commoncf}\VST3\iisaacbeats.cn');

    if (CompareText(RemoveBackslashUnlessRoot(WizardDirValue), RemoveBackslashUnlessRoot(DefaultVst3Path)) <> 0) and (not InstallDirWarningShown) then
    begin
      MsgBox('你选择了非默认VST3目录。安装完成后，你可能需要在宿主软件(DAW)中手动添加该目录并重新扫描插件，才能正常识别并使用本插件。', mbInformation, MB_OK);
      InstallDirWarningShown := True;
    end;
  end;
end;
