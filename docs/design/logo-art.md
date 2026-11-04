# 메인 화면 로고와 물결

첨부 원본 모양 그대로 복원한다는 사용자 확인에 따라, `logo-reference.png`의 실제 픽셀 윤곽을 사용한다. AI가 재해석했던 로고와 닫힌 고리 형태는 사용하지 않는다. 원본의 획과 열린 부분을 유지하고, 작은 원본의 가장자리만 표시 크기에 맞춰 정리했다. 원본과 축소 비교한 실루엣의 교집합/합집합 비율은 99.3%다.

로고는 위치·크기·밝기를 고정한다. 로고 테두리에서 청록색 물결 세 겹이 바깥으로 퍼지며 사라진다. 각 물결은 5초 주기로 반복하고, 중심 로고는 움직이지 않는다. 원형 고리나 로고 확대 대신, 원본 윤곽 바깥의 거리별 선을 사용한다. 스캔·설정·결과 화면에서는 기존 홈 전용 타이머 조건에 따라 정지한다. 웹 미리보기는 모션 줄이기 설정도 지원한다. 이 애니메이션은 UI 동작 표시이며 서버 연결 상태 표시는 기존 Wi-Fi 아이콘이 담당한다.

- 원본 참고: `firmware/assets/source/logo-reference.png`
- 화면·웹 미리보기: `firmware/assets/source/logo-art.png`
- 흰 배경 JPG: `firmware/assets/source/logo-art.jpg`
- LCD용: `firmware/main/assets/logo_art.a8` (332×332, 110,224 bytes)
- 거리별 윤곽 47개: `firmware/main/logo_contours.h`, `firmware/assets/source/logo-ripples.json`
- 재변환: `python firmware/tools/gen_logo_art.py` (Pillow, numpy, opencv-python 필요)
- 화면 확인: `docs/design/home-motion-preview.html`

PC에서 원본 로고를 네이티브 A8 마스크와 윤곽 좌표로 변환한다. 펌웨어는 확대·축소 없이 고정 크기 로고를 표시하고, 사전 계산된 윤곽 좌표를 선으로 그린다. 기존 AI 생성 원본과 RGB565 이미지는 사용 대상에서 제외하고 무시되는 `output/firmware-design/rejected-logo`에 보관했다.

검증: 원본 실루엣 비교, 네이티브 마스크와 PNG 알파 일치, 47개 윤곽의 바깥 방향 증가와 영역 내 배치를 확인했다. 웹 미리보기에서 프레임 변화와 모션 줄이기 설정을 검사했다. 실제 보드 업로드·LCD 확인은 아직 하지 않았다.

기존 ESP-IDF 5.5.5 환경에서 최종 `ninja -C firmware/build`가 성공했다(exit 0). 앱 파티션의 34%가 남는다. 기존 아이콘 112개와 고정 문구 16개 검사도 통과했다. 웹 렌더링에서 5초 분량 50프레임을 추출해 `output/playwright/logo-ripples.gif`에 저장했다. 이 GIF는 PC 미리보기이며 LCD 캡처가 아니다.
