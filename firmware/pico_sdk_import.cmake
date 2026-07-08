# Minimal Pico SDK import wrapper for this project.
# It uses PICO_SDK_PATH when set, otherwise tries ../third_party/pico-sdk.

if(DEFINED ENV{PICO_SDK_PATH})
    file(TO_CMAKE_PATH "$ENV{PICO_SDK_PATH}" PICO_SDK_PATH_FROM_ENV)
endif()

set(_PICO_SDK_CANDIDATES)

if(DEFINED PICO_SDK_PATH)
    list(APPEND _PICO_SDK_CANDIDATES "${PICO_SDK_PATH}")
endif()

if(DEFINED PICO_SDK_PATH_FROM_ENV)
    list(APPEND _PICO_SDK_CANDIDATES "${PICO_SDK_PATH_FROM_ENV}")
endif()

list(APPEND _PICO_SDK_CANDIDATES "${CMAKE_CURRENT_LIST_DIR}/../third_party/pico-sdk")

foreach(_candidate IN LISTS _PICO_SDK_CANDIDATES)
    if(EXISTS "${_candidate}/external/pico_sdk_import.cmake")
        set(PICO_SDK_PATH "${_candidate}" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
        include("${_candidate}/external/pico_sdk_import.cmake")
        return()
    endif()
endforeach()

message(FATAL_ERROR
    "Pico SDK was not found. Set PICO_SDK_PATH or run scripts/bootstrap-pico-sdk.ps1 "
    "to clone it into third_party/pico-sdk.")

