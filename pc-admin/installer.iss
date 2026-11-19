; Inno Setup 스크립트: dist\LabAdmin 폴더를 설치 파일 하나로 묶는다.
; build.ps1이 ISCC.exe로 컴파일한다. 결과: dist\LabAdmin-Setup-<버전>.exe
; 설치하면 "설정 > 앱 > 설치된 앱"에 나오고, 거기서 제거할 수 있다.

#define AppName "실습실 출입통제 관리자"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
; AppId는 바꾸지 않는다 (업데이트·제거가 같은 앱으로 인식되도록)
AppId={{6B3E2A71-4C0D-4F1B-9C54-2D7E8A1F0B93}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Lab QR Access
AppPublisherURL=https://lab.audreaming.com
AppSupportURL=https://lab.audreaming.com
DefaultDirName={autopf}\LabQrAccess Admin
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=dist
OutputBaseFilename=LabAdmin-Setup-{#AppVersion}
SetupIconFile=..\branding\icon.ico
UninstallDisplayIcon={app}\LabAdmin.exe
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; 앱이 켜져 있으면 설치·제거 전에 닫도록 안내 (lab_admin/app.py의 MUTEX_NAME과 같아야 함)
AppMutex=LabQrAccessAdminSingleInstance
CloseApplications=yes
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} 설치

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[Tasks]
Name: "desktopicon"; Description: "바탕 화면에 바로 가기 만들기"; GroupDescription: "추가 작업:"

[Files]
Source: "dist\LabAdmin\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\LabAdmin.exe"; AppUserModelID: "LabQrAccess.Admin"
Name: "{group}\{#AppName} 제거"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\LabAdmin.exe"; Tasks: desktopicon; AppUserModelID: "LabQrAccess.Admin"

[Run]
Filename: "{app}\LabAdmin.exe"; Description: "{#AppName} 실행"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 로그인 유지 정보(%APPDATA%\LabAdmin)는 제거할 때 함께 지운다
Type: filesandordirs; Name: "{userappdata}\LabAdmin"
