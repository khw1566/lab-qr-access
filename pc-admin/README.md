# 실습실 출입통제 관리자 (Windows PC 앱)

웹 관리자(`server/public/index.html`)의 관리 기능을 그대로 옮긴 Windows 데스크톱 앱입니다.
Python + PySide6(Qt)로 만든 자체 화면이며, 같은 서버 API(`https://lab.audreaming.com/api/*`)를 씁니다.

## 기능

| 화면 | 할 수 있는 일 |
|---|---|
| 현황 | 대기 건수, 오늘 출입 허용/거부, 긴급 잠금 경고, 단말 연결 상태, 원격 열기, 실시간 보기 |
| 이용 신청 승인 | 승인 대기·승인됨·반려 목록, 승인, 반려/승인 취소(관리자 멘트), 지도교수 서명 보기 |
| 특별 출입신청 | 상태별 목록, 승인·키 발급, 반려, 승인 취소 |
| 사용자 | 상태별·검색·페이지, 정보 수정, 비밀번호 변경, 출입권한 부여, 가입 승인/반려, 정지/해제, 관리자 지정/해제, 가입 정보 삭제 |
| 마스터 QR | 발급(화면용 5분/인쇄용 고정), 화면용 QR 자동 갱신(수동 입력 코드 포함), 인쇄용 QR 이미지 저장·인쇄, 재발급, 폐기 |
| 장비 상태 | 프린터 배치도에서 사용 가능 ↔ 점검 중 전환 |
| 실습실·자동 승인 | 실습실 추가, 이름·운영 여부·자동 승인 기준 저장 |
| 출입 단말 | 단말 등록(키 1회 표시·복사), 정보 수정, 키 폐기, 원격 열기, 실시간 보기, 실시간 보기 기록 |
| 출입 기록 | 기간·결과·실습실·단말·검색어·정렬·페이지, **CSV로 저장**(PC 앱 추가 기능) |
| 기능 설정 | 긴급 잠금, 마스터 QR, 문 열림 시간, 실시간 보기/소리 듣기, 신청·가입 설정, 화면 문구 |

그 밖에: 알림함(새 알림은 Windows 알림으로 표시), 15초마다 데이터 변경 확인 후 자동 새로고침, F5 새로고침,
로그인 유지(14일, Windows DPAPI로 암호화해 `%APPDATA%\LabAdmin`에 저장), 서버 주소 변경, 중복 실행 방지.

실시간 보기는 웹과 같은 WebSocket 형식으로 단말 카메라 영상, 단말 소리 듣기(기능 설정에서 허용 시), PC 마이크로 말하기를 지원합니다.

## 개발 실행

```powershell
py -3.12 -m venv .venv
.venv\Scripts\pip install -r requirements.txt
.venv\Scripts\python -m lab_admin
```

서버 없이 화면만 점검: `.venv\Scripts\python tests\smoke_ui.py 출력폴더` (가짜 데이터로 10개 화면 스크린샷)

## 설치 파일 만들기

필요: Python 3.12, [Inno Setup 6](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`)

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

결과: `dist\LabAdmin-Setup-<버전>.exe`. 설치하면 시작 메뉴와 **설정 > 앱 > 설치된 앱**에 "실습실 출입통제 관리자"로 나오고,
거기서 제거할 수 있습니다. 관리자 권한 없이 현재 사용자로 설치하며(설치 화면에서 모든 사용자 설치 선택 가능),
제거하면 로그인 유지 정보도 함께 지웁니다. 버전은 `lab_admin/__init__.py`의 `__version__`과 `version_info.txt`에서 바꿉니다.

아이콘 원본은 저장소의 `branding/` 폴더에 있습니다 (안드로이드 관리자 앱과 같은 아이콘).
