cmake_minimum_required(VERSION 3.20)
project(DiskPro VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()
if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING)
  add_compile_options(/W4 /utf-8 /permissive-)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_library(diskpro_core STATIC src/core/layout.cpp src/core/map.cpp src/core/tools.cpp src/core/restore.cpp)
target_include_directories(diskpro_core PUBLIC src/core)

add_executable(diskpro-cli src/cli/main.cpp)
target_link_libraries(diskpro-cli PRIVATE diskpro_core)

if(WIN32)
  add_library(diskpro_win STATIC src/platform/win/inventory.cpp src/platform/win/smart.cpp src/platform/win/device.cpp)
  target_include_directories(diskpro_win PUBLIC src/platform/win)
  target_link_libraries(diskpro_win PUBLIC diskpro_core)
  add_executable(diskpro-restore src/cli/restore_main.cpp)
  target_link_libraries(diskpro-restore PRIVATE diskpro_win)
  add_executable(diskpro-tools src/cli/tools_main.cpp)
  target_link_libraries(diskpro-tools PRIVATE diskpro_win)
  add_executable(diskpro-inventory src/cli/inventory_main.cpp)
  target_link_libraries(diskpro-inventory PRIVATE diskpro_win)
endif()

option(DISKPRO_BUILD_GUI "Build the Slint GUI" ON)
if(WIN32 AND DISKPRO_BUILD_GUI)
  list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/.tools/slint")
  find_package(Slint QUIET)
  if(Slint_FOUND)
    add_executable(diskpro-gui WIN32 src/app/main.cpp)
    slint_target_sources(diskpro-gui ui/main.slint)
    target_include_directories(diskpro-gui PRIVATE src/app)
    target_link_libraries(diskpro-gui PRIVATE Slint::Slint diskpro_core diskpro_win comdlg32)
    if(MSVC)
      target_link_options(diskpro-gui PRIVATE /ENTRY:mainCRTStartup)
    endif()
    add_custom_command(TARGET diskpro-gui POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/.tools/slint/lib/slint_cpp.dll" $<TARGET_FILE_DIR:diskpro-gui>)
  else()
    message(WARNING "Slint not found in .tools/slint: GUI not built")
  endif()
endif()

enable_testing()
add_executable(diskpro-tests tests/test_layout.cpp)
target_link_libraries(diskpro-tests PRIVATE diskpro_core)
add_test(NAME layout COMMAND diskpro-tests)
add_executable(diskpro-tests-tools tests/test_tools.cpp)
target_link_libraries(diskpro-tests-tools PRIVATE diskpro_core)
add_test(NAME tools COMMAND diskpro-tests-tools)
add_executable(diskpro-tests-restore tests/test_restore.cpp)
target_link_libraries(diskpro-tests-restore PRIVATE diskpro_core)
add_test(NAME restore COMMAND diskpro-tests-restore)







