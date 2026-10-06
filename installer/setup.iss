; MobiMic installer. Build with:  ISCC /DAppVersion=1.0.0 installer\setup.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6B0E4C0B-8C0B-4D56-9B5E-6D1C2B1A7E42}
AppName=MobiMic
AppVersion={#AppVersion}
AppPublisher=MobiMic
AppPublisherURL=https://aryanjsawant.github.io/MobiMic/
AppSupportURL=https://github.com/aryanjsawant/MobiMic/issues
DefaultDirName={autopf}\MobiMic
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
OutputDir=Output
OutputBaseFilename=MobiMic-Setup-{#AppVersion}
SetupIconFile=..\assets\icon.ico
WizardSmallImageFile=wizard-small.bmp
UninstallDisplayIcon={app}\MobiMic.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Add a desktop shortcut for the MobiMic app"; Flags: unchecked

[Files]
; The plugin, for DAWs.
Source: "..\plugin\build\MobiMic_artefacts\Release\VST3\MobiMic.vst3\*"; DestDir: "{commoncf64}\VST3\MobiMic.vst3"; Flags: recursesubdirs ignoreversion
; The standalone app.
Source: "..\plugin\build\MobiMic_artefacts\Release\Standalone\MobiMic.exe"; DestDir: "{app}"; Flags: ignoreversion
; The Ableton Live helper that places finished takes on the timeline.
Source: "..\ableton\MobiMic\*"; DestDir: "{userdocs}\Ableton\User Library\Remote Scripts\MobiMic"; Flags: recursesubdirs ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"

[Icons]
Name: "{autoprograms}\MobiMic"; Filename: "{app}\MobiMic.exe"
Name: "{autodesktop}\MobiMic"; Filename: "{app}\MobiMic.exe"; Tasks: desktopicon

[Run]
; The phone connects over the local network, so MobiMic's ports must be allowed in.
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""MobiMic"""; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""MobiMic"" dir=in action=allow protocol=TCP localport=8443-8452 profile=any"; Flags: runhidden; StatusMsg: "Allowing your phone through the firewall..."

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""MobiMic"""; Flags: runhidden; RunOnceId: "RemoveFirewallRule"

[Messages]
FinishedLabel=MobiMic is installed.%n%nIn a DAW: rescan plug-ins if needed and add MobiMic to an audio track.%nOn its own: open MobiMic from the Start menu.%n%nThen scan the QR code with your phone.%n%nAbleton Live: to have takes placed on the track for you, choose MobiMic as a Control Surface in Preferences > Link, Tempo & MIDI.
