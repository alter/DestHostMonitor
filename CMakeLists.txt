cmake_minimum_required(VERSION 3.21)
project(pingtrace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

# FTXUI for the live dashboard (dom + screen only; no interactive event loop).
include(FetchContent)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0)
FetchContent_MakeAvailable(ftxui)

add_executable(pingtrace
  src/main.cpp
  src/cli.cpp
  src/types.cpp
  src/config.cpp
  src/scheduler.cpp
  src/rollup.cpp
  src/events.cpp
  src/analyzer.cpp
  src/exporter.cpp
  src/discover.cpp
  src/prober/icmp_probe.cpp
  src/prober/tcp_probe.cpp
  src/prober/traceroute.cpp
  src/prober/prober.cpp
  src/storage/record_codec.cpp
  src/storage/segment_writer.cpp
  src/storage/segment_reader.cpp
  src/storage/index.cpp
  src/storage/seal.cpp
  src/storage/address_registry.cpp
  src/util/time.cpp
  src/util/log.cpp
)

target_include_directories(pingtrace PRIVATE src)

target_link_libraries(pingtrace PRIVATE
  nlohmann_json::nlohmann_json
  zstd::libzstd
  ftxui::screen
  ftxui::dom
  Iphlpapi
  Ws2_32
)
