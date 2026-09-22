cmake_minimum_required(VERSION 3.20)

project(ydyun_guestctl LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "ydyun-guestctl is a Windows utility; configure it on Windows.")
endif()

add_executable(ydyun-guestctl
  src/ydyun_guestctl.cpp
)

target_compile_features(ydyun-guestctl PRIVATE cxx_std_17)
target_compile_definitions(ydyun-guestctl PRIVATE UNICODE _UNICODE)
target_link_libraries(ydyun-guestctl PRIVATE advapi32 setupapi)

if(MSVC)
  target_compile_options(ydyun-guestctl PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(ydyun-guestctl PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_link_options(ydyun-guestctl PRIVATE -municode -static)
endif()
