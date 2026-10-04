if(NOT DEFINED CHECKER OR NOT EXISTS "${CHECKER}")
    message(FATAL_ERROR "The actual 3DS scene checker is required")
endif()

execute_process(COMMAND "${CHECKER}" --help
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output MATCHES "BOOT \\(240 frames\\) and LEVEL1_1 \\(1440 frames\\)"
    OR NOT output MATCHES "SOURCE_FRAMES: 1..3600"
    OR NOT output MATCHES "--all-optics: check every requested frame at strength 2")
    message(FATAL_ERROR "Scene checker help/default scope changed: ${output}${error}")
endif()

# Validate before opening private assets. A generic missing-ROM failure is
# insufficient: every bad bound must identify the SOURCE_FRAMES argument.
foreach(frames IN ITEMS 0 3601 -1 1.5 240x 4294967296)
    foreach(optics IN ITEMS default maximum)
    set(optics_argument)
    if(optics STREQUAL maximum)
        set(optics_argument --all-optics)
    endif()
    execute_process(COMMAND "${CHECKER}" missing-private-ROM missing-private-SYMBOLS LEVEL1_3 "${frames}"
        ${optics_argument}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 1 OR NOT error MATCHES "SOURCE_FRAMES must be a whole number from 1 through 3600")
        message(FATAL_ERROR "Incorrect frame-count rejection for '${frames}': ${result} / ${output}${error}")
    endif()
    endforeach()
endforeach()

execute_process(COMMAND "${CHECKER}" ROM SYMBOLS LEVEL1_3 240 extra
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 1 OR NOT error MATCHES "Usage: check_3ds_game_models")
    message(FATAL_ERROR "Unexpected extra-argument handling: ${result} / ${output}${error}")
endif()
execute_process(COMMAND "${CHECKER}" ROM SYMBOLS LEVEL1_3 240 --all-optics extra
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 1 OR NOT error MATCHES "Usage: check_3ds_game_models")
    message(FATAL_ERROR "Unexpected all-optics extra-argument handling: ${result} / ${output}${error}")
endif()
message(STATUS "Scene checker help/defaults, twelve invalid bounds and both extra-argument rejections passed")
