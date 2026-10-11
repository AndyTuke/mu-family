; mu-Clid MIDI Inno Setup installer script
; Build with: iscc /DBuildNum=<N> mu-Clid-MIDI.iss
; Or via CMake: cmake --build build --target mu-clid-lite_installer --config Release
; NOTE: file names + installer text are ASCII "mu-Clid" — Inno Setup mangles the
; Greek mu (μ) in paths under Windows codepages. The plugin's DAW display name is
; still "μ-Clid MIDI" (set via PLUGIN_NAME in CMake, built with /utf-8).

#ifndef BuildNum
  #define BuildNum "0"
#endif

#define MyAppName      "mu-Clid MIDI"
#ifndef VersionBase
  #define VersionBase "1.1"
#endif
#define MyAppVersion   VersionBase + "." + BuildNum
#define MyAppPublisher "Transwarp Development Project"
; Paths relative to mu-clid/installer/. Build output lives at family root.
#define SourceDir      "..\..\build\mu-clid\mu-clid-lite_artefacts\Release"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppVerName={#MyAppName} {#MyAppVersion}
VersionInfoVersion={#MyAppVersion}

DefaultDirName={autopf}\Transwarp Development Project\mu-Clid MIDI
DisableDirPage=yes

OutputDir=..\..\build\installer
OutputBaseFilename=mu-Clid-MIDI-Setup-v{#MyAppVersion}

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
Name: "full";    Description: "Full installation (VST3 + CLAP)"
Name: "custom";  Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 Plugin  →  %ProgramFiles%\Common Files\VST3\"; Types: full
Name: "clap"; Description: "CLAP Plugin   →  %ProgramFiles%\Common Files\CLAP\"; Types: full

[InstallDelete]
; The product was renamed from "mu-Clid Lite": remove the old bundles so a DAW never sees two plugins with the same ID.
Type: filesandordirs; Name: "{commoncf64}\VST3\mu-Clid Lite.vst3"
Type: files; Name: "{commoncf64}\CLAP\mu-Clid Lite.clap"

[Files]
; Licence, third-party notices and source pointer travel with the binaries (GPLv3).
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\SOURCE.txt"; DestDir: "{app}"; Flags: ignoreversion
; VST3 — copy the entire .vst3 bundle folder recursively.
Source: "{#SourceDir}\VST3\mu-Clid MIDI.vst3\*"; \
    DestDir: "{commoncf64}\VST3\mu-Clid MIDI.vst3"; \
    Components: vst3; \
    Flags: recursesubdirs createallsubdirs ignoreversion

; CLAP — single binary.
Source: "{#SourceDir}\CLAP\mu-Clid MIDI.clap"; \
    DestDir: "{commoncf64}\CLAP"; \
    Components: clap; \
    Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{commoncf64}\VST3\mu-Clid MIDI.vst3\Contents\x86_64-win\mu-Clid MIDI.vst3"

[Run]
; No post-install run step needed for plugin-only installers.
