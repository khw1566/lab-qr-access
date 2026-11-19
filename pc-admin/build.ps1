# 실습실 출입통제 관리자 - Windows 설치 파일 빌드
#   powershell -ExecutionPolicy Bypass -File build.ps1
# 1) 아이콘(PNG/ICO) 다시 만들기  2) PyInstaller 폴더 빌드  3) Inno Setup으로 설치 파일 만들기
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$py = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (-not (Test-Path $py)) {
  py -3.12 -m venv .venv
  & $py -m pip install -r requirements.txt
}

$version = (& $py -c "import lab_admin; print(lab_admin.__version__)").Trim()
Write-Host "버전 $version"

& $py ..\branding\make_icons.py
& $py -m PyInstaller --noconfirm --clean lab_admin.spec
if ($LASTEXITCODE -ne 0) { throw 'PyInstaller 실패' }

$iscc = @(
  "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
  "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
  "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6이 없습니다: winget install JRSoftware.InnoSetup' }

& $iscc "/DAppVersion=$version" installer.iss
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup 실패' }
Write-Host "완료: dist\LabAdmin-Setup-$version.exe"
