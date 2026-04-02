; ============================================================
; DADBADBASS — Inno Setup Installer Script
; Targets: VST3 (Windows 10/11 x64)
; ============================================================

#define MyAppName      "DADBADBASS"
#define MyAppVersion   "1.0.0"
#define MyAppPublisher "DadLabs"
#define MyAppURL       "https://dadbadbass.io"

#ifndef MyVST3Path
  #define MyVST3Path "..\..\Build\DadBass_artefacts\Release\VST3\DADBADBASS.vst3"
#endif
#ifndef MyOutputDir
  #define MyOutputDir "Output"
#endif

[Setup]
AppId={{A3B7C2D1-F8E4-4A2B-9C6D-1E5F3A8B2C7D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={commonpf64}\Common Files\VST3
DefaultGroupName={#MyAppPublisher}
DisableDirPage=yes
OutputDir={#MyOutputDir}
OutputBaseFilename=DADBADBASS_Setup_{#MyAppVersion}_Win64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

[Files]
Source: "{#MyVST3Path}\*"; \
        DestDir: "{commonpf64}\Common Files\VST3\DADBADBASS.vst3"; \
        Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%nDADBADBASSS.vst3 will be installed to:%n  C:\Program Files\Common Files\VST3\DADBADBASS.vst3%n%nOpen your DAW and scan for new plugins after installation.
