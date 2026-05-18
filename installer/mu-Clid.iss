; μ-Clid Inno Setup installer script
; Build with: iscc /DBuildNum=152 mu-Clid.iss
; Or via CMake: cmake --build build --target mu-clid_installer --config Release
; NOTE: this .iss file must be saved as UTF-8 with BOM so Inno Setup
; reads the Greek mu (μ) correctly in MyAppName / paths / output filename.

#ifndef BuildNum
  #define BuildNum "0"
#endif

#define MyAppName      "μ-Clid"
#define MyAppVersion   "1.0." + BuildNum
#define MyAppPublisher "Transwarp Development Project"
#define MyAppExeName   "μ-Clid.exe"
#define SourceDir      "..\build\mu-clid_artefacts\Release"
#define ContentSrc     "..\content"
#define DefaultContent "{userdocs}\TDP\muClid"

[Setup]
; Keep this AppId constant — changing it breaks uninstall/upgrade detection.
AppId={{7E8F9A2B-3C4D-5E6F-7A8B-9C0D1E2F3A4B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppVerName={#MyAppName} {#MyAppVersion}
VersionInfoVersion={#MyAppVersion}

; Standalone goes to a user-chosen location; plugins go to fixed system paths.
DefaultDirName={autopf}\Transwarp Development Project\μ-Clid
DisableDirPage=no
DirExistsWarning=no

OutputDir=..\build\installer
OutputBaseFilename=μ-Clid-Setup-v{#MyAppVersion}

ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64

SetupIconFile=..\resources\Icon256.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern

; No signing configured yet — add SignTool= here when a cert is available.
; SignTool=signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /f cert.pfx /p $p $f

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";    Description: "Full installation (VST3 + CLAP + Standalone)"
Name: "plugins"; Description: "Plugins only (VST3 + CLAP)"
Name: "compact"; Description: "Standalone only"
Name: "custom";  Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 Plugin  →  %ProgramFiles%\Common Files\VST3\";  Types: full plugins
Name: "clap";       Description: "CLAP Plugin   →  %ProgramFiles%\Common Files\CLAP\"; Types: full plugins
Name: "standalone"; Description: "Standalone Application";                              Types: full compact

[Files]
; VST3 — copy the entire .vst3 bundle folder recursively.
Source: "{#SourceDir}\VST3\μ-Clid.vst3\*"; \
    DestDir: "{commoncf64}\VST3\μ-Clid.vst3"; \
    Components: vst3; \
    Flags: recursesubdirs createallsubdirs ignoreversion

; CLAP — single binary.
Source: "{#SourceDir}\CLAP\μ-Clid.clap"; \
    DestDir: "{commoncf64}\CLAP"; \
    Components: clap; \
    Flags: ignoreversion

; Standalone — installs to the directory chosen on the directory page.
Source: "{#SourceDir}\Standalone\{#MyAppExeName}"; \
    DestDir: "{app}"; \
    Components: standalone; \
    Flags: ignoreversion

; SoundTouch DLL — uncomment when SoundTouch ships as a DLL (LGPL compliance).
; Source: "{#SourceDir}\Standalone\SoundTouch.dll"; DestDir: "{app}";            Components: standalone; Flags: ignoreversion
; Source: "{#SourceDir}\VST3\SoundTouch.dll";       DestDir: "{commoncf64}\VST3"; Components: vst3;       Flags: ignoreversion
; Source: "{#SourceDir}\CLAP\SoundTouch.dll";        DestDir: "{commoncf64}\CLAP"; Components: clap;       Flags: ignoreversion

; Factory presets — copied into the user content folder chosen on the content page.
; Add .muclid files to content\Presets\ in the repo and they will be bundled automatically.
Source: "{#ContentSrc}\Presets\*.muclid"; \
    DestDir: "{code:GetContentDir}\Presets"; \
    Flags: skipifsourcedoesntexist ignoreversion

; Factory rhythm presets.
Source: "{#ContentSrc}\Rhythms\*.muRhyth"; \
    DestDir: "{code:GetContentDir}\Rhythms"; \
    Flags: skipifsourcedoesntexist ignoreversion

[Dirs]
; Create the content folder tree at the user-chosen location.
; .gitkeep files in the repo keep these dirs tracked but are not copied.
Name: "{code:GetContentDir}\Presets"
Name: "{code:GetContentDir}\Rhythms"
Name: "{code:GetContentDir}\Samples"

[Icons]
Name: "{group}\{#MyAppName}";           Filename: "{app}\{#MyAppExeName}"; Components: standalone
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}";        Components: standalone

[Run]
Filename: "{app}\{#MyAppExeName}"; \
    Description: "Launch {#MyAppName}"; \
    Flags: nowait postinstall skipifsilent; \
    Components: standalone

[UninstallDelete]
Type: dirifempty; Name: "{app}"

[Code]
var
  ContentDirPage: TInputDirWizardPage;

procedure InitializeWizard();
begin
  // Custom page: let the user choose (or confirm) the content folder location.
  // Inserted after the standard directory page so the flow is:
  //   Welcome → Components → Standalone dir → Content dir → Ready → Install
  ContentDirPage := CreateInputDirPage(
    wpSelectDir,
    'Content Folder',
    'Where should μ-Clid store presets, rhythms, and samples?',
    'Factory presets will be copied here. You can change this later in the μ-Clid Settings overlay.',
    False,
    'Browse for content folder'
  );
  ContentDirPage.Add('Content folder:');
  ContentDirPage.Values[0] := ExpandConstant('{#DefaultContent}');
end;

// Called by [Files] DestDir and [Dirs] Name entries via {code:GetContentDir}.
function GetContentDir(Param: String): String;
begin
  Result := ContentDirPage.Values[0];
end;

procedure WriteSettingsFile(ContentDir: String);
// Writes (or overwrites) %APPDATA%\TDP\muClid.xml so the plugin finds the
// chosen content folder immediately on first launch, without the user having
// to open Settings and set it manually.
var
  SettingsDir: String;
  SettingsFile: String;
  Xml: String;
begin
  SettingsDir := ExpandConstant('{userappdata}\TDP');
  ForceDirectories(SettingsDir);
  SettingsFile := SettingsDir + '\muClid.xml';

  // Only write if the file does not already exist (preserve user's existing settings on reinstall).
  if not FileExists(SettingsFile) then
  begin
    Xml := '<?xml version="1.0" encoding="UTF-8"?>'            + #13#10 +
           ''                                                    + #13#10 +
           '<PROPERTIES>'                                        + #13#10 +
           '  <VALUE name="contentDir" val="' + ContentDir + '"/>' + #13#10 +
           '</PROPERTIES>';
    SaveStringToFile(SettingsFile, Xml, False);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ContentDir: String;
  DefaultDir: String;
begin
  if CurStep = ssPostInstall then
  begin
    ContentDir := ContentDirPage.Values[0];
    DefaultDir := ExpandConstant('{#DefaultContent}');

    // Always write the settings file when the user chose a non-default path,
    // so the plugin doesn't silently fall back to the default on first run.
    if ContentDir <> DefaultDir then
      WriteSettingsFile(ContentDir);
  end;
end;
