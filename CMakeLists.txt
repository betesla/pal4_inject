cmake_minimum_required(VERSION 3.19)
project(pal4_inject VERSION 0.1.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PAL4_INJECT_BUILD_TESTS "Build PAL4 inject tests" ON)

if(NOT WIN32)
    message(FATAL_ERROR "pal4_inject requires Windows.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "pal4_inject requires a 32-bit / Win32 generator.")
endif()

set(PAL4_INJECT_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(PAL4_INJECT_PAL4_RE_ROOT ${PAL4_INJECT_REPO_ROOT})
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../pal4_re/rebuild/include")
    set(PAL4_INJECT_PAL4_RE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../pal4_re")
endif()

set(PAL4_INJECT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(PAL4_INJECT_BUILD_INFO_HEADER ${PAL4_INJECT_GENERATED_DIR}/pal4inject_build_info.h)

add_custom_target(pal4_inject_build_info ALL
    COMMAND ${CMAKE_COMMAND}
        -DVERSION=v${PROJECT_VERSION}
        -DOUTPUT=${PAL4_INJECT_BUILD_INFO_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_build_info.cmake
    BYPRODUCTS ${PAL4_INJECT_BUILD_INFO_HEADER}
    COMMENT "Generating PAL4 inject build info"
)

add_library(pal4_inject_common STATIC
    src/common/camera_pitch_guard.cpp
    src/common/camera_unlock_patch.cpp
    src/common/cegui_font_experiment.cpp
    src/common/cegui_font_resync.cpp
    src/common/cegui_widescreen.cpp
    src/common/crash_capture.cpp
    src/common/dpi_awareness.cpp
    src/common/hook_inventory.cpp
    src/common/inject_control_panel.cpp
    src/common/inject_settings.cpp
    src/common/input_logic.cpp
    src/common/input_queue.cpp
    src/common/launcher.cpp
    src/common/memory_debug.cpp
    src/common/protocol.cpp
    src/common/runtime_paths.cpp
    src/common/script_mode_override.cpp
    src/common/ui_snapshot.cpp
    src/common/types.cpp
)
target_include_directories(pal4_inject_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PAL4_INJECT_GENERATED_DIR}
        ${PAL4_INJECT_PAL4_RE_ROOT}/rebuild/include
)
target_link_libraries(pal4_inject_common PUBLIC advapi32 user32)
add_dependencies(pal4_inject_common pal4_inject_build_info)

add_library(pal4_inject_runtime_core STATIC
    src/runtime/battle_ui_layout_hooks.cpp
    src/runtime/bootstrap.cpp
    src/runtime/cegui_font_hooks.cpp
    src/runtime/cegui_font_experiment.cpp
    src/runtime/cegui_font_texture_registry.cpp
    src/runtime/d3d9_quality_hooks.cpp
    src/runtime/cegui_renderer_hooks.cpp
    src/runtime/camera_hooks.cpp
    src/runtime/camera_unlock.cpp
    src/runtime/cegui_bindings.cpp
    src/runtime/dialog_pagination_hooks.cpp
    src/runtime/crash_handler.cpp
    src/runtime/hud_layout_fixups.cpp
    src/runtime/hook_logging.cpp
    src/runtime/hook_manager.cpp
    src/runtime/inject_control_window.cpp
    src/runtime/input_hooks.cpp
    src/runtime/ipc_server.cpp
    src/runtime/memory_debug_runtime.cpp
    src/runtime/minimap_hooks.cpp
    src/runtime/runtime_preferences.cpp
    src/runtime/runtime_state.cpp
    src/runtime/ui_snapshot_runtime.cpp
    src/runtime/window_focus_hooks.cpp
)
target_include_directories(pal4_inject_runtime_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PAL4_INJECT_GENERATED_DIR}
        ${PAL4_INJECT_PAL4_RE_ROOT}/rebuild/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
)
target_link_libraries(pal4_inject_runtime_core PUBLIC pal4_inject_common advapi32 user32)
add_dependencies(pal4_inject_runtime_core pal4_inject_build_info)

add_library(pal4_runtime_x86 SHARED
    src/runtime/dll_main.cpp
)
target_link_libraries(pal4_runtime_x86 PRIVATE pal4_inject_runtime_core)
set_target_properties(pal4_runtime_x86 PROPERTIES
    OUTPUT_NAME "runtime"
)

add_executable(pal4_injector_launcher
    src/launcher/main.cpp
)
target_link_libraries(pal4_injector_launcher PRIVATE pal4_inject_common)
target_link_libraries(pal4_injector_launcher PRIVATE gdi32 comctl32 wininet)
set_target_properties(pal4_injector_launcher PROPERTIES
    OUTPUT_NAME "PAL4_inject"
    WIN32_EXECUTABLE TRUE
)
if(MSVC)
    target_link_options(pal4_injector_launcher PRIVATE /ENTRY:mainCRTStartup)
endif()

add_executable(pal4_inject_cli
    src/cli/main.cpp
)
target_link_libraries(pal4_inject_cli PRIVATE pal4_inject_common)
set_target_properties(pal4_inject_cli PROPERTIES
    OUTPUT_NAME "cli"
)

if(PAL4_INJECT_BUILD_TESTS)
    enable_testing()
    add_executable(pal4_inject_tests
        tests/inject_tests_main.cpp
    )
    target_include_directories(pal4_inject_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
    )
    target_link_libraries(pal4_inject_tests PRIVATE pal4_inject_runtime_core)
    add_dependencies(pal4_inject_tests pal4_inject_build_info)
    add_test(NAME pal4_inject_tests COMMAND pal4_inject_tests)
endif()

foreach(target
    pal4_inject_common
    pal4_inject_runtime_core
    pal4_runtime_x86
    pal4_inject_cli
    pal4_injector_launcher
    pal4_inject_tests
)
    if(TARGET ${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive- /FS /utf-8)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
endforeach()
