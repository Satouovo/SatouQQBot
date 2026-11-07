cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(SatouQQBot LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Satou QQBot requires Windows and the Windows SDK.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(satou_core STATIC
  src/util.cpp
  src/duration.cpp
  src/config.cpp
  src/perm.cpp
  src/message.cpp
  src/store.cpp
  src/onebot.cpp
  src/guard.cpp
  src/commands.cpp
  src/dispatch.cpp
  src/runner.cpp
  src/approve.cpp
  third_party/easywsclient/easywsclient.cpp
)

target_include_directories(satou_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(satou_core SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

if(MSVC)
  target_compile_options(satou_core PUBLIC /utf-8 /W4 /MP /external:W0 /external:anglebrackets)
  set_source_files_properties(third_party/easywsclient/easywsclient.cpp PROPERTIES COMPILE_OPTIONS "/W0")
  target_compile_definitions(satou_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_link_libraries(satou_core PUBLIC ws2_32)

add_executable(SatouQQBot WIN32 src/main.cpp assets/app.rc)
target_compile_definitions(SatouQQBot PRIVATE UNICODE _UNICODE)
target_link_libraries(SatouQQBot PRIVATE satou_core shell32 comctl32)
set_target_properties(SatouQQBot PROPERTIES VS_DPI_AWARE "PerMonitor")

include(CTest)
if(BUILD_TESTING)
  add_executable(SatouQQBotTests tests/test_main.cpp)
  target_link_libraries(SatouQQBotTests PRIVATE satou_core)
  add_test(NAME satou_tests COMMAND SatouQQBotTests)
  set_tests_properties(satou_tests PROPERTIES TIMEOUT 60)
endif()

install(TARGETS SatouQQBot RUNTIME DESTINATION .)
install(FILES config.example.json README.md LICENSE DESTINATION .)
install(FILES third_party/easywsclient/LICENSE DESTINATION licenses/easywsclient)
install(FILES third_party/nlohmann/LICENSE.MIT DESTINATION licenses/nlohmann)
