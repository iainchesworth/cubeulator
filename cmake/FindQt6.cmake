# ---------------------------------------------------------------------------
# FindQt6.cmake - wrapper picked up automatically by find_package(Qt6 ...)
# because cmake/ is on CMAKE_MODULE_PATH (CMake tries Module mode, i.e. a
# Find<Pkg>.cmake, before Config mode).
#
# It never replaces Qt6Config.cmake: it only widens CMAKE_PREFIX_PATH with
# the aqtinstall / install-qt-action layout used by ci/install-qt.ps1 and
# ci/install-qt.sh (C:/Qt, C:/Repos/common/qt, ~/Qt, /opt/Qt, ...) when Qt6 isn't already
# discoverable, then defers to Qt's own config package for the real work.
# An explicit -DCMAKE_PREFIX_PATH=... or -DQt6_DIR=... always wins.
# ---------------------------------------------------------------------------

find_package(Qt6 ${Qt6_FIND_VERSION} CONFIG QUIET COMPONENTS ${Qt6_FIND_COMPONENTS})

if(NOT Qt6_FOUND)
    # Where an aqtinstall-style Qt lives is a fact about the platform, so it
    # is declared by the platform's fragment (cmake/Platform.cmake).
    set(_qt6_search_roots ${CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS})
    set(_qt6_arch_dirs ${CUBEULATOR_PLATFORM_QT_ARCH_DIRS})

    set(_qt6_candidates "")
    foreach(root IN LISTS _qt6_search_roots)
        if(NOT root OR NOT IS_DIRECTORY "${root}")
            continue()
        endif()

        file(GLOB _qt6_version_dirs LIST_DIRECTORIES true "${root}/6.*")
        foreach(version_dir IN LISTS _qt6_version_dirs)
            if(NOT IS_DIRECTORY "${version_dir}")
                continue()
            endif()
            foreach(arch IN LISTS _qt6_arch_dirs)
                if(EXISTS "${version_dir}/${arch}/lib/cmake/Qt6/Qt6Config.cmake")
                    list(APPEND _qt6_candidates "${version_dir}/${arch}")
                endif()
            endforeach()
        endforeach()

        # System-wide Qt6 installs (e.g. apt's qt6-base-dev) ship directly under root.
        if(EXISTS "${root}/lib/cmake/Qt6/Qt6Config.cmake")
            list(APPEND _qt6_candidates "${root}")
        endif()
    endforeach()

    if(_qt6_candidates)
        list(SORT _qt6_candidates COMPARE NATURAL)
        list(REVERSE _qt6_candidates)
        list(GET _qt6_candidates 0 _qt6_chosen)

        message(STATUS "FindQt6: auto-detected prebuilt Qt6 at ${_qt6_chosen} (pass -DCMAKE_PREFIX_PATH to override)")
        list(APPEND CMAKE_PREFIX_PATH "${_qt6_chosen}")

        set(_qt6_args CONFIG)
        if(Qt6_FIND_QUIETLY)
            list(APPEND _qt6_args QUIET)
        endif()
        if(Qt6_FIND_REQUIRED)
            list(APPEND _qt6_args REQUIRED)
        endif()
        if(Qt6_FIND_COMPONENTS)
            list(APPEND _qt6_args COMPONENTS ${Qt6_FIND_COMPONENTS})
        endif()

        find_package(Qt6 ${Qt6_FIND_VERSION} ${_qt6_args})

        unset(_qt6_args)
        unset(_qt6_chosen)
    endif()

    unset(_qt6_search_roots)
    unset(_qt6_arch_dirs)
    unset(_qt6_candidates)
    unset(_qt6_version_dirs)
endif()

# A find module has to report failure itself: unlike Config mode, CMake does
# not turn "not found" into an error for REQUIRED. Without this the configure
# carries on and dies later at qt_standard_project_setup() with "Unknown CMake
# command", which says nothing about Qt being missing.
if(NOT Qt6_FOUND AND Qt6_FIND_REQUIRED)
    message(FATAL_ERROR
        "Could not find Qt6 (>= ${Qt6_FIND_VERSION}) and no prebuilt Qt was found "
        "in the platform's usual install locations. Pass -DCMAKE_PREFIX_PATH="
        "<Qt install>/<kit> (for example C:/Qt/6.12.0/msvc2022_64), or select a "
        "preset that sets it. Or configure with -DCUBEULATOR_BUILD_APP=OFF for a "
        "build without the GUI.")
endif()
