# 작업 스케줄러가 부팅 시 실행하는 래퍼. 서버가 죽으면 3초 뒤 다시 띄우고, 로그는 data\logs\server.log 에 남긴다.
param([Parameter(Mandatory = $true)][string]$Node)
$ErrorActionPreference = 'Continue'
$ServerDir = Split-Path -Parent $PSScriptRoot
Set-Location $ServerDir
$LogDir = Join-Path $ServerDir 'data\logs'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$Log = Join-Path $LogDir 'server.log'

while ($true) {
    # 로그가 10MB를 넘으면 한 번 돌려 둔다 (server.log.1)
    if ((Test-Path $Log) -and ((Get-Item $Log).Length -gt 10MB)) {
        Move-Item -Force $Log "$Log.1"
    }
    Add-Content -Path $Log -Value "==== $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') 서버 시작" -Encoding UTF8
    & $Node --disable-warning=ExperimentalWarning node/server.js *>> $Log
    Add-Content -Path $Log -Value "==== $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') 서버 종료 (코드 $LASTEXITCODE), 3초 뒤 재시작" -Encoding UTF8
    Start-Sleep -Seconds 3
}
