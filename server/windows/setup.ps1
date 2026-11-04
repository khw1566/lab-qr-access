# Windows 미니PC 설치 스크립트. 관리자 PowerShell에서 server 폴더로 이동한 뒤 실행:
#   Set-ExecutionPolicy -Scope Process Bypass; .\windows\setup.ps1
# 하는 일: Node.js 확인/설치, QR 라이브러리, .env, 부팅 시 자동 실행(작업 스케줄러), 매일 백업, 방화벽, 절전 끄기, cloudflared 설치
$ErrorActionPreference = 'Stop'
$ServerDir = Split-Path -Parent $PSScriptRoot
Set-Location $ServerDir

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw '관리자 권한 PowerShell에서 실행하세요. (시작 메뉴 > PowerShell 우클릭 > 관리자 권한으로 실행)' }

function Refresh-Path {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

Write-Host '== 1. Node.js 22.13 이상 확인' -ForegroundColor Cyan
$nodeOk = $false
$nodeCmd = Get-Command node -ErrorAction SilentlyContinue
if ($nodeCmd) {
    $v = [version](& node -p 'process.versions.node')
    $nodeOk = $v -ge [version]'22.13.0'
}
if (-not $nodeOk) {
    winget install --id OpenJS.NodeJS.LTS -e --scope machine --accept-source-agreements --accept-package-agreements
    Refresh-Path
    $nodeCmd = Get-Command node -ErrorAction Stop
}
$Node = $nodeCmd.Source
Write-Host "node: $Node ($(& $Node -v))"

Write-Host '== 2. QR 라이브러리' -ForegroundColor Cyan
$qr = Join-Path $ServerDir 'public\vendor\qrcode.js'
if (-not (Test-Path $qr)) {
    $tmp = Join-Path $env:TEMP ("qrlib-" + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $tmp | Out-Null
    Push-Location $tmp
    npm pack --silent qrcode-generator@1.4.4 | Out-Null
    tar -xzf (Get-ChildItem qrcode-generator-*.tgz).Name
    Pop-Location
    New-Item -ItemType Directory -Force -Path (Split-Path $qr) | Out-Null
    Copy-Item (Join-Path $tmp 'package\qrcode.js') $qr
    Remove-Item -Recurse -Force $tmp
}

Write-Host '== 3. 설정 파일 (.env)' -ForegroundColor Cyan
$envFile = Join-Path $ServerDir '.env'
if (-not (Test-Path $envFile)) {
    Copy-Item (Join-Path $ServerDir 'node\env.example') $envFile
}
$envText = Get-Content $envFile -Raw
if ($envText -match '(?m)^SESSION_SECRET=\s*$' -or $envText -match '(?m)^QR_SECRET=\s*$') {
    Write-Host "$envFile 에 SESSION_SECRET, QR_SECRET, ADMIN_SETUP_CODE 를 채운 뒤 이 스크립트를 다시 실행하세요." -ForegroundColor Yellow
    notepad $envFile
    exit 0
}
# .env 는 관리자와 SYSTEM만 읽을 수 있게
icacls $envFile /inheritance:r /grant:r 'Administrators:F' 'SYSTEM:F' | Out-Null

Write-Host '== 4. 부팅 시 자동 실행 (작업 스케줄러: LabAccessServer)' -ForegroundColor Cyan
$ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$runner = Join-Path $PSScriptRoot 'run-server.ps1'
$principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -MultipleInstances IgnoreNew
$action = New-ScheduledTaskAction -Execute $ps -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$runner`" -Node `"$Node`"" -WorkingDirectory $ServerDir
Register-ScheduledTask -TaskName 'LabAccessServer' -Action $action -Trigger (New-ScheduledTaskTrigger -AtStartup) `
    -Principal $principal -Settings $settings -Description '실습실 출입통제 서버' -Force | Out-Null

Write-Host '== 5. 매일 03:00 DB 백업 (LabAccessBackup)' -ForegroundColor Cyan
$backupAction = New-ScheduledTaskAction -Execute $Node -Argument '--disable-warning=ExperimentalWarning node/backup.js' -WorkingDirectory $ServerDir
$backupSettings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName 'LabAccessBackup' -Action $backupAction -Trigger (New-ScheduledTaskTrigger -Daily -At 3am) `
    -Principal $principal -Settings $backupSettings -Description '실습실 출입통제 DB 백업' -Force | Out-Null

Write-Host '== 6. 방화벽: 단말 포트 8788 을 같은 망(LocalSubnet)에서만 허용' -ForegroundColor Cyan
$port = 8788
if ($envText -match '(?m)^DEVICE_PORT=(\d+)') { $port = [int]$Matches[1] }
Get-NetFirewallRule -DisplayName 'Lab Access device API' -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -DisplayName 'Lab Access device API' -Direction Inbound -Protocol TCP -LocalPort $port `
    -RemoteAddress LocalSubnet -Action Allow -Profile Any | Out-Null

Write-Host '== 7. 절전·최대 절전 끄기 (전원 연결 시)' -ForegroundColor Cyan
powercfg /change standby-timeout-ac 0
powercfg /change hibernate-timeout-ac 0

Write-Host '== 8. 서버 시작' -ForegroundColor Cyan
Stop-ScheduledTask -TaskName 'LabAccessServer' -ErrorAction SilentlyContinue
Get-CimInstance Win32_Process -Filter "Name='node.exe'" | Where-Object { $_.CommandLine -like '*node/server.js*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Start-ScheduledTask -TaskName 'LabAccessServer'
Start-Sleep -Seconds 4
$pubPort = 8787
if ($envText -match '(?m)^PUBLIC_PORT=(\d+)') { $pubPort = [int]$Matches[1] }
try {
    Invoke-RestMethod "http://127.0.0.1:$pubPort/healthz" | Out-Null
    Write-Host '서버가 실행 중입니다.' -ForegroundColor Green
} catch {
    Write-Host "서버 응답이 없습니다. 로그 확인: $ServerDir\data\logs\server.log" -ForegroundColor Red
}

Write-Host '== 9. cloudflared 설치' -ForegroundColor Cyan
if (-not (Get-Command cloudflared -ErrorAction SilentlyContinue)) {
    winget install --id Cloudflare.cloudflared -e --accept-source-agreements --accept-package-agreements
    Refresh-Path
}

$ip = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } | Select-Object -First 1).IPAddress
Write-Host @"

설치 완료. 남은 일:
 1) Cloudflare 대시보드 > Zero Trust > Networks > Tunnels 에서 터널을 만들고
    나오는 'cloudflared.exe service install <토큰>' 명령을 이 관리자 PowerShell에서 실행
 2) 터널의 Public hostname: lab.audreaming.com -> http://localhost:$pubPort
    (먼저 Workers > lab-access > Settings > Domains 에서 lab.audreaming.com 연결 해제)
 3) 보드 menuconfig 의 Device API URL 을 http://${ip}:$port 로 설정
자세한 순서: docs\windows-minipc.md
"@
