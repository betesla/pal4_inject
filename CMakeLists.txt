cmake_minimum_required(VERSION 3.19)
project(pal4_inject VERSION 0.1.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PAL4_INJECT_BUILD_TESTS "Build PAL4 inject tests" ON)
option(PAL4_INJECT_SYNC_TEST_DEPLOY "Auto-copy build artifacts to dist and local test game root after build" ON)

set(PAL4_INJECT_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH "PAL4 inject dist output directory")
set(PAL4_INJECT_TEST_GAME_ROOT "I:/Games/original" CACHE PATH "Local PAL4 install used for quick manual testing")

if(NOT WIN32)
    message(FATAL_ERROR "pal4_inject requires Windows.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "pal4_inject requires a 32-bit / Win32 generator.")
endif()

set(PAL4_INJECT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(PAL4_INJECT_BUILD_INFO_HEADER ${PAL4_INJECT_GENERATED_DIR}/pal4inject_build_info.h)

function(pal4_inject_append_copy_command out_commands source destination)
    list(APPEND ${out_commands}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${source}" "${destination}"
    )
    set(${out_commands} "${${out_commands}}" PARENT_SCOPE)
endfunction()

function(pal4_inject_add_runtime_deploy_target)
    if(NOT PAL4_INJECT_SYNC_TEST_DEPLOY)
        return()
    endif()

    set(copy_commands)
    pal4_inject_append_copy_command(
        copy_commands
        "$<TARGET_FILE:pal4_injector_launcher>"
        "${PAL4_INJECT_DIST_DIR}"
    )
    pal4_inject_append_copy_command(
        copy_commands
        "$<TARGET_FILE:pal4_runtime_x86>"
        "${PAL4_INJECT_DIST_DIR}/pal4_inject"
    )
    pal4_inject_append_copy_command(
        copy_commands
        "$<TARGET_FILE:pal4_inject_cli>"
        "${PAL4_INJECT_DIST_DIR}/pal4_inject"
    )

    if(PAL4_INJECT_TEST_GAME_ROOT)
        pal4_inject_append_copy_command(
            copy_commands
            "$<TARGET_FILE:pal4_injector_launcher>"
            "${PAL4_INJECT_TEST_GAME_ROOT}"
        )
        pal4_inject_append_copy_command(
            copy_commands
            "$<TARGET_FILE:pal4_runtime_x86>"
            "${PAL4_INJECT_TEST_GAME_ROOT}/pal4_inject"
        )
        pal4_inject_append_copy_command(
            copy_commands
            "$<TARGET_FILE:pal4_inject_cli>"
            "${PAL4_INJECT_TEST_GAME_ROOT}/pal4_inject"
        )
    endif()

    add_custom_target(pal4_inject_sync_deploy ALL
        DEPENDS
            pal4_runtime_x86
            pal4_injector_launcher
            pal4_inject_cli
        ${copy_commands}
        VERBATIM
        COMMENT "Syncing PAL4 inject artifacts to dist and test game root"
    )
endfunction()

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
    src/common/gamepad.cpp
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
    src/runtime/crash_handler.cpp
    src/runtime/gamepad_runtime.cpp
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
    src/runtime/shadow_quality_hooks.cpp
    src/runtime/ui_snapshot_runtime.cpp
    src/runtime/window_focus_hooks.cpp
)
target_include_directories(pal4_inject_runtime_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PAL4_INJECT_GENERATED_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
)
target_link_libraries(pal4_inject_runtime_core PUBLIC pal4_inject_common advapi32 user32 winmm)
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

pal4_inject_add_runtime_deploy_target()

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
