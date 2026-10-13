# ---------------------------------------------------------------------------
# PlatformContract.cmake - what a platform fragment
# (src/app/platform/<name>/platform.cmake) has to declare, and the check that it
# did. The contract itself is described at the top of cmake/Platform.cmake.
#
# Shared by the selector (which checks the one fragment it includes, at
# configure time, so a platform that forgets part of the contract fails by
# name) and by tests/platform/check_fragments.cmake (which checks every
# fragment in the tree on every host, so the platforms a leg does not build
# are still held to it).
# ---------------------------------------------------------------------------
include_guard(GLOBAL)

set(CUBE_PLATFORM_CONTRACT_VARIABLES
    CUBEULATOR_PLATFORM_DISPLAY_NAME
    CUBEULATOR_PLATFORM_DESKTOP
    CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM
    CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES
    CUBEULATOR_PLATFORM_TEST_DISPLAY_HELP
    CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS
    CUBEULATOR_PLATFORM_QT_ARCH_DIRS
    CUBEULATOR_PLATFORM_SOURCES
    CUBEULATOR_PLATFORM_LINK_LIBRARIES
    CUBEULATOR_PLATFORM_QT_LINK_LIBRARIES
    CUBEULATOR_PLATFORM_COMPILE_DEFINITIONS
    CUBEULATOR_PLATFORM_CONTROLLER_SOURCES
    CUBEULATOR_PLATFORM_CONTROLLER_LINK_LIBRARIES
    CUBEULATOR_PLATFORM_APP_PROPERTIES)

set(CUBE_PLATFORM_CONTRACT_HOOKS
    cube_platform_prepare_dependencies
    cube_platform_deploy_qt_runtime
    cube_platform_deploy_qt_library
    cube_platform_configure_packaging)

# Forgets whatever a previous fragment declared, so that checking several in a
# row cannot let one pass on another's variables. A macro: it has to unset in
# the caller's scope.
macro(cube_platform_reset_contract)
    foreach(_cube_contract_variable IN LISTS CUBE_PLATFORM_CONTRACT_VARIABLES)
        unset(${_cube_contract_variable})
    endforeach()
    unset(_cube_contract_variable)
endmacro()

# Checks the fragment that was just included (in the caller's scope):
#   - every contract variable is defined (an empty string is a definition);
#   - every hook exists;
#   - CUBEULATOR_PLATFORM_DESKTOP is a boolean;
#   - every file the fragment compiles exists under <app_dir> (src/app).
# `fragment` only names it in the message.
function(cube_platform_check_contract fragment app_dir)
    foreach(_variable IN LISTS CUBE_PLATFORM_CONTRACT_VARIABLES)
        if(NOT DEFINED ${_variable})
            message(FATAL_ERROR
                "${fragment} does not set ${_variable}. Set it to an empty "
                "string if the platform has nothing to say (the contract is "
                "described at the top of cmake/Platform.cmake).")
        endif()
    endforeach()

    foreach(_hook IN LISTS CUBE_PLATFORM_CONTRACT_HOOKS)
        if(NOT COMMAND ${_hook})
            message(FATAL_ERROR
                "${fragment} does not define ${_hook}(). Define it as a "
                "no-op if the platform has nothing to do.")
        endif()
    endforeach()

    if(NOT CUBEULATOR_PLATFORM_DESKTOP STREQUAL "ON" AND NOT CUBEULATOR_PLATFORM_DESKTOP STREQUAL "OFF")
        message(FATAL_ERROR
            "${fragment}: CUBEULATOR_PLATFORM_DESKTOP must be ON or OFF, "
            "not '${CUBEULATOR_PLATFORM_DESKTOP}'.")
    endif()

    foreach(_file IN LISTS CUBEULATOR_PLATFORM_SOURCES CUBEULATOR_PLATFORM_CONTROLLER_SOURCES)
        if(NOT EXISTS "${app_dir}/${_file}")
            message(FATAL_ERROR
                "${fragment} lists ${_file}, which does not exist under "
                "${app_dir}.")
        endif()
    endforeach()
endfunction()
