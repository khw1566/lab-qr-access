cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/version.cmake)
# esp_wifi_remote가 이 값으로 Kconfig를 고르므로 비어 있으면 C6 대신 다른 코프로세서가 선택된다
set(ENV{ESP_IDF_VERSION} "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lab_door)
