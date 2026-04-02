; BadDadVerb Windows Installer
; Generated for use with Inno Setup 6+

#define AppName      "BadDadVerb"
#define AppVersion   "0.1.0"
#define AppPublisher "DadLabs"
#define AppURL       "https://github.com/mezkal000/BadDadVerb"
#define VST3Dir      "..\dist\VST3\BadDadVerb.vst3"

[Setup]
AppId={{A3D90A00-8D4A-45C6-9CC7-06AFE2970FD7}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
AppPublisher={#AppPublisher}
DefaultDirName={commonpf64}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}
AllowNoIcons=yes
OutputDir=Output
OutputBaseFilename=BadDadVerb_Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; No signing — set SignTool= here when you have a certificate
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";    Description: "Full Installation (VST3)"
Name: "custom";  Description: "Custom Installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 Plugin (recommended for most DAWs)"; Types: full custom; Flags: fixed

[Files]
; ── VST3 — installs to the system-wide VST3 folder ──────────────────────────
Source: "{#VST3Dir}\*"; DestDir: "{cf64}\VST3\BadDadVerb.vst3"; \
  Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3

; ── Licence / readme ─────────────────────────────────────────────────────────
; Uncomment if you add these files later:
; Source: "..\LICENSE";   DestDir: "{app}"; Flags: ignoreversion
; Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

[Run]
; Optional: open DAW plugin scanner note after install
; Filename: "notepad.exe"; Parameters: "{app}\README.md"; \
;   Description: "View readme"; Flags: postinstall skipifsilent

[Messages]
FinishedLabel=BadDadVerb has been installed.%n%nThe VST3 plugin is in:%n  C:\Program Files\Common Files\VST3\BadDadVerb.vst3%n%nRescan plugins in your DAW to start using it.
