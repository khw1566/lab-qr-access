idf_component_register(
    SRCS "ov02c10.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_cam_sensor esp_driver_gpio
)
# 감지 함수는 링커 섹션으로만 참조되므로 오브젝트가 빠지지 않게 심볼을 지정한다
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ov02c10_detect")
