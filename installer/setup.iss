; MobiMic installer. Build with:  ISCC /DAppVersion=0.1.0 installer\setup.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6B0E4C0B-8C0B-4D56-9B5E-6D1C2B1A7E42}
AppName=MobiMic
AppVersion={#AppVersion}
AppPublisher=MobiMic
DefaultDirName={autopf}\MobiMic
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
OutputDir=Output
OutputBaseFilename=MobiMic-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "..\plugin\build\MobiMic_artefacts\Release\VST3\MobiMic.vst3\*"; DestDir: "{commoncf64}\VST3\MobiMic.vst3"; Flags: recursesubdirs ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"

[Run]
; The phone connects to the plugin over the local network, so the plugin's ports must be allowed in.
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""MobiMic"""; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""MobiMic"" dir=in action=allow protocol=TCP localport=8443-8452 profile=any"; Flags: runhidden; StatusMsg: "Allowing your phone through the firewall..."

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""MobiMic"""; Flags: runhidden; RunOnceId: "RemoveFirewallRule"

[Messages]
FinishedLabel=MobiMic is installed.%n%nOpen your DAW, rescan plug-ins if needed, and add MobiMic to an audio track. Then scan the QR code with your phone.
