; =============================================================
; AKSIOMA Knots - Windows installer (Inno Setup 6.7 or newer)
; =============================================================
;
; StageDir is the folder with the program and its Qt files (windeployqt),
; ManualDir the user manuals and AppVersion the version; build_installer.ps1
; passes all three. The installer is written to output\AKSIOMA-Knots-<version>-Setup.exe.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

#ifndef StageDir
  #define StageDir "build\stage"
#endif

#ifndef ManualDir
  #define ManualDir "..\..\Manual"
#endif

#define AppName "AKSIOMA Knots"
#define AppExe "AKSIOMAKnots.exe"
#define AppPublisher "AKSIOMA"

[Setup]
; The AppId is how a newer version finds the installed one (fixed for good).
AppId={{75678F91-CD50-4EB2-B275-595845BAC7F7}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright=Copyright © 2026 AKSIOMA
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup

; For the current user without administrator rights by default, or for all
; users (the first page asks)
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
UsePreviousAppDir=yes

; Qt 6: 64-bit Windows 10 or newer
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

LicenseFile=..\LICENSE

OutputDir=output
OutputBaseFilename=AKSIOMA-Knots-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes

; The look: dark like the program, with the AKSIOMA logo
WizardStyle=modern dark
WizardImageFile=images\wizard_164x314.png,images\wizard_205x392.png,images\wizard_246x471.png,images\wizard_328x628.png,images\wizard_410x785.png
WizardSmallImageFile=images\small_55x58.png,images\small_69x72.png,images\small_82x87.png,images\small_110x116.png,images\small_138x145.png
WizardImageBackColor=#090909
; the Welcome page with the logo (the modern style leaves it out otherwise)
DisableWelcomePage=no
SetupIconFile=..\icons\app_icon.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}

; English or Albanian, asked at the start (and remembered for updates)
ShowLanguageDialog=yes

; .aknot files get the program's icon
ChangesAssociations=yes

; a running copy is closed before its files are replaced
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "sq"; MessagesFile: "Albanian.isl"

[CustomMessages]
en.AppComment=Knot theory in 3D: knots, invariants, unknotting and Seifert surfaces
sq.AppComment=Teoria e nyjeve në 3D: nyje, invariante, zgjidhja e nyjeve dhe sipërfaqet e Seifertit
en.FileTypeName=AKSIOMA knot file
sq.FileTypeName=Skedar nyjeje AKSIOMA
en.OpenManual=Open the user manual
sq.OpenManual=Hap manualin e përdorimit
en.ManualFile=AKSIOMA_Knots_Manual_EN.pdf
sq.ManualFile=AKSIOMA_Knots_Manual_SQ.pdf

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; the program with its Qt and MinGW libraries (windeployqt)
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

; the user manuals (English, Albanian, and both in one)
Source: "{#ManualDir}\AKSIOMA_Knots_Manual_EN.pdf"; DestDir: "{app}\Manual"; Flags: ignoreversion
Source: "{#ManualDir}\AKSIOMA_Knots_Manual_SQ.pdf"; DestDir: "{app}\Manual"; Flags: ignoreversion
Source: "{#ManualDir}\AKSIOMA_Knots_Manual_EN_SQ.pdf"; DestDir: "{app}\Manual"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "{cm:AppComment}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "{cm:AppComment}"; Tasks: desktopicon

[Registry]
; .aknot files: the program's icon, and a double click opens them. HKA is
; the current user's or all users' classes, as the program is installed.
; (The program also registers them for the current user when it starts,
; so that a moved copy keeps working.)
Root: HKA; Subkey: "Software\Classes\.aknot"; ValueType: string; ValueName: ""; ValueData: "AKSIOMA.Knot"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\AKSIOMA.Knot"; ValueType: string; ValueName: ""; ValueData: "{cm:FileTypeName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\AKSIOMA.Knot\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"",0"
Root: HKA; Subkey: "Software\Classes\AKSIOMA.Knot\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""

; what the program registered for the current user is removed with it
Root: HKCU; Subkey: "Software\Classes\.aknot"; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\AKSIOMA.Knot"; Flags: dontcreatekey uninsdeletekey

; the program starts in the language chosen here (unless it was set before)
Root: HKCU; Subkey: "Software\AKSIOMA\AKSIOMA Knots"; ValueType: string; ValueName: "language"; ValueData: "en"; Languages: en; Flags: createvalueifdoesntexist
Root: HKCU; Subkey: "Software\AKSIOMA\AKSIOMA Knots"; ValueType: string; ValueName: "language"; ValueData: "sq"; Languages: sq; Flags: createvalueifdoesntexist

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\Manual\{cm:ManualFile}"; Description: "{cm:OpenManual}"; Flags: shellexec nowait postinstall skipifsilent unchecked
