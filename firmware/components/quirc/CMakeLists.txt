# QR 디코더 quirc (ISC 라이선스, https://github.com/dlbeer/quirc) - lib/ 에 소스 포함
idf_component_register(
    SRCS "lib/decode.c" "lib/identify.c" "lib/quirc.c" "lib/version_db.c"
    INCLUDE_DIRS "lib"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -Wno-unused-variable -Wno-unused-function)
