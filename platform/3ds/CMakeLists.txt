cmake_minimum_required(VERSION 3.22)
project(starfox_3ds_frontend LANGUAGES CXX)
option(STARFOX_3DS_BUILD_NATIVE "Build the asset-free libctru diagnostic (not the game)" OFF)
option(STARFOX_3DS_BUILD_GPU_CHECK "Include the native Citro3D diagnostic (not the game)" ON)
option(STARFOX_3DS_BUILD_HOST_TESTS "Build platform-independent projection/LCD tests" ON)
option(STARFOX_3DS_BUILD_GAME_CORE "Link the actual cartridge VM/SPC/game session bring-up target" ${STARFOX_3DS_BUILD_NATIVE})
add_library(starfox_3ds_frontend STATIC frontend.cpp)
target_include_directories(starfox_3ds_frontend PUBLIC ../../include)
target_compile_features(starfox_3ds_frontend PUBLIC cxx_std_20)
add_library(starfox_3ds_source_geometry STATIC pica_shapes.cpp
    ../../src/render/software_renderer.cpp ../../src/render/face_material.cpp
    ../../src/simulation/math.cpp ../../src/assets/rom.cpp)
target_include_directories(starfox_3ds_source_geometry PUBLIC ../../include)
target_compile_features(starfox_3ds_source_geometry PUBLIC cxx_std_20)
add_library(starfox_3ds_raster STATIC pica_raster.cpp
    ../../src/render/background_renderer.cpp ../../src/render/sprite_renderer.cpp)
target_include_directories(starfox_3ds_raster PUBLIC ../../include)
target_compile_features(starfox_3ds_raster PUBLIC cxx_std_20)
if(STARFOX_3DS_BUILD_GAME_CORE)
    include(cartridge_core.cmake)
endif()
if(STARFOX_3DS_BUILD_NATIVE)
    if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
        message(FATAL_ERROR "Use devkitPro's 3DS.cmake toolchain; a desktop compiler cannot build a 3DS app.")
    endif()
    find_path(STARFOX_CTRU_INCLUDE 3ds.h HINTS "${DEVKITPRO}/libctru/include" "$ENV{DEVKITPRO}/libctru/include" REQUIRED)
    find_library(STARFOX_CTRU_LIBRARY ctru HINTS "${DEVKITPRO}/libctru/lib" "$ENV{DEVKITPRO}/libctru/lib" REQUIRED)
    # The lean-core target compiles/links these actual sources. Retain the
    # limited ABI-only check when explicitly building without the VM/SPC core.
    if(NOT STARFOX_3DS_BUILD_GAME_CORE)
        add_library(starfox_3ds_game_models_arm_syntax OBJECT game_models.cpp
            ../../src/vr/source_pose_interpolation.cpp)
        target_include_directories(starfox_3ds_game_models_arm_syntax PRIVATE ../../include)
        target_compile_features(starfox_3ds_game_models_arm_syntax PRIVATE cxx_std_20)
    endif()
    add_library(starfox_3ds_native_audio STATIC native_audio.cpp)
    target_link_libraries(starfox_3ds_native_audio PUBLIC starfox_3ds_frontend PRIVATE "${STARFOX_CTRU_LIBRARY}")
    target_include_directories(starfox_3ds_native_audio SYSTEM PRIVATE "${STARFOX_CTRU_INCLUDE}")
    add_executable(starfox_3ds_frontend_check frontend_probe.cpp native_display.cpp)
    target_include_directories(starfox_3ds_frontend_check SYSTEM PRIVATE "${STARFOX_CTRU_INCLUDE}")
    target_link_libraries(starfox_3ds_frontend_check PRIVATE starfox_3ds_frontend starfox_3ds_native_audio "${STARFOX_CTRU_LIBRARY}" m)
    set_target_properties(starfox_3ds_frontend_check PROPERTIES SUFFIX ".elf")
    if(NOT COMMAND ctr_create_3dsx)
        message(FATAL_ERROR "The current devkitPro 3DS CMake helpers are required (3ds-cmake).")
    endif()
    ctr_generate_smdh(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/frontend-check.smdh"
        NAME "Star Fox Enhanced - frontend check"
        DESCRIPTION "Asset-free LCD, input and stereo diagnostic - not the game"
        AUTHOR "kandowontu")
    ctr_create_3dsx(starfox_3ds_frontend_check SMDH "${CMAKE_CURRENT_BINARY_DIR}/frontend-check.smdh")
    if(STARFOX_3DS_BUILD_GAME_CORE)
        include(companion_manifest.cmake)
        add_executable(starfox_3ds_game_core_check game_core_probe.cpp native_display.cpp "${companion_header}")
        target_include_directories(starfox_3ds_game_core_check PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_include_directories(starfox_3ds_game_core_check SYSTEM PRIVATE "${STARFOX_CTRU_INCLUDE}")
        target_link_libraries(starfox_3ds_game_core_check PRIVATE starfox_3ds_cartridge_core
            starfox_3ds_native_audio "${STARFOX_CTRU_LIBRARY}" m)
        set_target_properties(starfox_3ds_game_core_check PROPERTIES SUFFIX ".elf")
        ctr_generate_smdh(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/game-core-check.smdh"
            NAME "Star Fox Enhanced - source core"
            DESCRIPTION "Actual cartridge VM / SPC / HUD bring-up - incomplete renderer"
            AUTHOR "kandowontu")
        ctr_create_3dsx(starfox_3ds_game_core_check SMDH "${CMAKE_CURRENT_BINARY_DIR}/game-core-check.smdh")
    endif()
    if(STARFOX_3DS_BUILD_GPU_CHECK)
        find_path(STARFOX_CITRO3D_INCLUDE citro3d.h HINTS "${DEVKITPRO}/libctru/include" "$ENV{DEVKITPRO}/libctru/include" REQUIRED)
        find_library(STARFOX_CITRO3D_LIBRARY citro3d HINTS "${DEVKITPRO}/libctru/lib" "$ENV{DEVKITPRO}/libctru/lib" REQUIRED)
        if(NOT COMMAND ctr_add_shader_library)
            message(FATAL_ERROR "The devkitPro Picasso/CMake shader helpers are required")
        endif()
        set(pica_shader "${CMAKE_CURRENT_BINARY_DIR}/pica_scene.shbin")
        ctr_add_shader_library(starfox_3ds_pica_shader OUTPUT "${pica_shader}" pica_scene.v.pica)
        set(pica_header "${CMAKE_CURRENT_BINARY_DIR}/generated/pica_scene_shader.hpp")
        add_custom_command(OUTPUT "${pica_header}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${pica_shader}" "-DOUTPUT=${pica_header}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_shader.cmake"
            DEPENDS starfox_3ds_pica_shader "${pica_shader}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_shader.cmake"
            VERBATIM)
        add_library(starfox_3ds_native_gpu STATIC native_gpu.cpp)
        target_link_libraries(starfox_3ds_native_gpu PUBLIC starfox_3ds_frontend
            PRIVATE "${STARFOX_CITRO3D_LIBRARY}" "${STARFOX_CTRU_LIBRARY}")
        target_include_directories(starfox_3ds_native_gpu SYSTEM PRIVATE
            "${STARFOX_CTRU_INCLUDE}" "${STARFOX_CITRO3D_INCLUDE}")
        add_executable(starfox_3ds_gpu_check gpu_probe.cpp native_display.cpp "${pica_header}")
        target_include_directories(starfox_3ds_gpu_check PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_include_directories(starfox_3ds_gpu_check SYSTEM PRIVATE "${STARFOX_CTRU_INCLUDE}")
        target_link_libraries(starfox_3ds_gpu_check PRIVATE starfox_3ds_native_gpu
            starfox_3ds_source_geometry starfox_3ds_raster "${STARFOX_CTRU_LIBRARY}" m)
        set_target_properties(starfox_3ds_gpu_check PROPERTIES SUFFIX ".elf")
        ctr_generate_smdh(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gpu-check.smdh"
            NAME "Star Fox Enhanced - GPU check"
            DESCRIPTION "Asset-free PICA textures, depth and stereo check - not the game"
            AUTHOR "kandowontu")
        ctr_create_3dsx(starfox_3ds_gpu_check SMDH "${CMAKE_CURRENT_BINARY_DIR}/gpu-check.smdh")
    endif()
elseif(STARFOX_3DS_BUILD_HOST_TESTS)
    enable_testing()
    # Compile the diagnostic owner without linking it to a fake display. This
    # catches ordinary C++/frontend API regressions when the native SDK is not
    # installed; it does not validate libctru or produce a desktop game.
    add_library(starfox_3ds_probe_syntax OBJECT frontend_probe.cpp)
    target_link_libraries(starfox_3ds_probe_syntax PRIVATE starfox_3ds_frontend)
    add_executable(starfox_3ds_frontend_tests ../../tests/nintendo_3ds_frontend_tests.cpp)
    target_link_libraries(starfox_3ds_frontend_tests PRIVATE starfox_3ds_frontend)
    add_test(NAME starfox_3ds_frontend_tests COMMAND starfox_3ds_frontend_tests)
    add_executable(starfox_3ds_audio_tests ../../tests/nintendo_3ds_audio_tests.cpp)
    target_link_libraries(starfox_3ds_audio_tests PRIVATE starfox_3ds_frontend)
    add_test(NAME starfox_3ds_audio_tests COMMAND starfox_3ds_audio_tests)
    add_executable(starfox_3ds_pica_tests ../../tests/nintendo_3ds_pica_tests.cpp)
    target_link_libraries(starfox_3ds_pica_tests PRIVATE starfox_3ds_frontend)
    add_test(NAME starfox_3ds_pica_tests COMMAND starfox_3ds_pica_tests)
    add_executable(starfox_3ds_source_geometry_tests ../../tests/nintendo_3ds_source_geometry_tests.cpp)
    target_link_libraries(starfox_3ds_source_geometry_tests PRIVATE starfox_3ds_source_geometry)
    add_test(NAME starfox_3ds_source_geometry_tests COMMAND starfox_3ds_source_geometry_tests)
    add_executable(starfox_3ds_source_models_check ../../tools/check_3ds_source_models.cpp
        ../../src/assets/shape_decoder.cpp)
    target_link_libraries(starfox_3ds_source_models_check PRIVATE starfox_3ds_source_geometry)
    add_executable(starfox_3ds_hud_routing_tests ../../tests/nintendo_3ds_hud_routing_tests.cpp
        ../../src/render/sprite_renderer.cpp)
    target_include_directories(starfox_3ds_hud_routing_tests PRIVATE ../../include)
    target_compile_features(starfox_3ds_hud_routing_tests PRIVATE cxx_std_20)
    add_test(NAME starfox_3ds_hud_routing_tests COMMAND starfox_3ds_hud_routing_tests)
    add_executable(starfox_3ds_raster_tests ../../tests/nintendo_3ds_raster_tests.cpp)
    target_link_libraries(starfox_3ds_raster_tests PRIVATE starfox_3ds_raster)
    add_test(NAME starfox_3ds_raster_tests COMMAND starfox_3ds_raster_tests)
    if(STARFOX_3DS_BUILD_GAME_CORE)
        include(companion_manifest.cmake)
        add_executable(starfox_3ds_game_assets_tests ../../tests/nintendo_3ds_game_assets_tests.cpp "${companion_header}")
        target_include_directories(starfox_3ds_game_assets_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_link_libraries(starfox_3ds_game_assets_tests PRIVATE starfox_3ds_cartridge_core)
        add_test(NAME starfox_3ds_game_assets_tests COMMAND starfox_3ds_game_assets_tests "${CMAKE_CURRENT_SOURCE_DIR}/../..")
        add_executable(starfox_3ds_game_session_check ../../tools/check_3ds_game_session.cpp)
        target_link_libraries(starfox_3ds_game_session_check PRIVATE starfox_3ds_cartridge_core)
        add_executable(starfox_3ds_game_models_check ../../tools/check_3ds_game_models.cpp)
        target_link_libraries(starfox_3ds_game_models_check PRIVATE starfox_3ds_cartridge_core)
    endif()
endif()
