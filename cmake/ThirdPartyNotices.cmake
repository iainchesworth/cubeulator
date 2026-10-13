# ---------------------------------------------------------------------------
# ThirdPartyNotices.cmake
#
# Third-party notices generated from the build, so they cannot drift from what
# ships. See docs/licensing.md for the design and ThirdPartyNoticesGenerate.cmake
# for where each fact comes from.
#
#   cube_add_third_party_notices(<app-target>)
#       Adds the cube_third_party_notices target (ALL), which writes
#       <build>/third_party_notices/third-party-notices.json and
#       THIRD-PARTY-NOTICES.txt before <app-target> is linked; embeds both in
#       <app-target> as the Qt resources
#           :/licences/third-party-notices.json
#           :/licences/THIRD-PARTY-NOTICES.txt
#       (for the future About and Licences screens); and installs the text
#       file, with LICENSE, into desktop packages. Installing refuses to
#       package while anything is unresolved.
#
#   cube_add_third_party_notices_test(<app-target>)
#       Registers the CTest case that fails when a component that ships has no
#       notice. Needs enable_testing(); the test blocks add it from tests/.
# ---------------------------------------------------------------------------
include_guard(GLOBAL)

# The generator reads the app's resolved link line from the file API. Issued
# here, at include time, so the reply exists for the very first build.
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)

function(_cube_notices_platform out)
    if(ANDROID)
        set(p android)
    elseif(IOS)
        set(p ios)
    elseif(WIN32)
        set(p windows)
    elseif(APPLE)
        set(p macos)
    else()
        set(p linux)
    endif()
    set(${out} "${p}" PARENT_SCOPE)
endfunction()

function(_cube_notices_common_args out app_target)
    _cube_notices_platform(platform)

    set(triplet_dir "")
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(triplet_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    elseif(DEFINED _VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(triplet_dir "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    endif()

    set(port_dirs "")
    foreach(d IN LISTS VCPKG_OVERLAY_PORTS)
        list(APPEND port_dirs "${d}")
    endforeach()
    list(APPEND port_dirs
        "${PROJECT_SOURCE_DIR}/vcpkg-overlay-ports"
        "${PROJECT_SOURCE_DIR}/deps/vcpkg/ports")

    set(qt_prefix "")
    if(DEFINED Qt6_DIR)
        get_filename_component(qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    endif()
    set(qt_version "")
    if(DEFINED Qt6_VERSION)
        set(qt_version "${Qt6_VERSION}")
    elseif(DEFINED Qt6Core_VERSION)
        set(qt_version "${Qt6Core_VERSION}")
    endif()

    set(args
        "-DCUBE_NOTICES_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DCUBE_NOTICES_BINARY_DIR=${PROJECT_BINARY_DIR}"
        "-DCUBE_NOTICES_TARGET=${app_target}"
        "-DCUBE_NOTICES_PLATFORM=${platform}"
        "-DCUBE_NOTICES_QT_PREFIX=${qt_prefix}"
        "-DCUBE_NOTICES_QT_VERSION=${qt_version}"
        "-DCUBE_NOTICES_VCPKG_TRIPLET_DIR=${triplet_dir}")
    # a ;-list inside one -D value would be split; join with a separator the
    # script is told about
    list(JOIN port_dirs "@SEP@" port_dirs_joined)
    list(APPEND args "-DCUBE_NOTICES_VCPKG_PORT_DIRS_JOINED=${port_dirs_joined}")
    set(${out} "${args}" PARENT_SCOPE)
endfunction()

function(cube_add_third_party_notices app_target)
    set(out_dir "${PROJECT_BINARY_DIR}/third_party_notices")
    set(json "${out_dir}/third-party-notices.json")
    set(txt "${out_dir}/THIRD-PARTY-NOTICES.txt")
    file(MAKE_DIRECTORY "${out_dir}")

    # Placeholders so qt_add_resources sees real files at configure time; the
    # target below replaces them with the real thing before anything links.
    if(NOT EXISTS "${json}")
        file(WRITE "${json}" "{\"schema\":1,\"components\":[],\"unresolved\":[\"not generated yet\"]}\n")
    endif()
    if(NOT EXISTS "${txt}")
        file(WRITE "${txt}" "Third-party notices have not been generated yet.\n")
    endif()

    _cube_notices_common_args(common "${app_target}")
    set_property(GLOBAL PROPERTY CUBE_NOTICES_COMMON_ARGS "${common}")
    set_property(GLOBAL PROPERTY CUBE_NOTICES_OUT_DIR "${out_dir}")

    set(inputs
        "${PROJECT_SOURCE_DIR}/third_party/notices/manifest.json"
        "${PROJECT_SOURCE_DIR}/third_party/notices/qt-modules.json"
        "${PROJECT_SOURCE_DIR}/cmake/ThirdPartyNoticesGenerate.cmake")

    # A target, not an OUTPUT rule: the link line it reads (and the vcpkg tree
    # beside it) can change without any file in `inputs` changing. The script
    # writes a file only when its content differs, so a no-op run costs
    # nothing downstream.
    add_custom_target(cube_third_party_notices ALL
        COMMAND "${CMAKE_COMMAND}" ${common}
            "-DCUBE_NOTICES_OUT_DIR=${out_dir}"
            -P "${PROJECT_SOURCE_DIR}/cmake/ThirdPartyNoticesGenerate.cmake"
        BYPRODUCTS "${json}" "${txt}"
        DEPENDS ${inputs}
        COMMENT "Generating third-party notices"
        VERBATIM)

    qt_add_resources(${app_target} "cube_third_party_notices"
        PREFIX "/licences"
        BASE "${out_dir}"
        FILES "${json}" "${txt}")
    add_dependencies(${app_target} cube_third_party_notices)

    # Desktop packages: the notices and the licence ride in every one. The
    # check comes first so a refused install leaves nothing behind.
    if(NOT ANDROID AND NOT IOS)
        install(CODE "
            file(READ \"${json}\" _notices)
            string(JSON _n LENGTH \"\${_notices}\" unresolved)
            if(_n GREATER 0)
                string(JSON _first GET \"\${_notices}\" unresolved 0)
                message(FATAL_ERROR \"Refusing to package: \${_n} third-party component(s) have no notice. First: \${_first}\nSee ${out_dir}/third-party-notices.json and docs/licensing.md.\")
            endif()
        ")
        install(FILES "${txt}" DESTINATION .)
        install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION . RENAME LICENSE.txt)
    endif()
endfunction()

function(cube_add_third_party_notices_test app_target)
    if(NOT TARGET cube_third_party_notices)
        return()
    endif()
    get_property(common GLOBAL PROPERTY CUBE_NOTICES_COMMON_ARGS)
    get_property(out_dir GLOBAL PROPERTY CUBE_NOTICES_OUT_DIR)
    add_test(NAME third_party_notices_complete
        COMMAND "${CMAKE_COMMAND}" ${common}
            "-DCUBE_NOTICES_OUT_DIR=${out_dir}"
            "-DCUBE_NOTICES_DEPLOY_DIR=$<TARGET_FILE_DIR:${app_target}>"
            "-DCUBE_NOTICES_GENERATOR=${PROJECT_SOURCE_DIR}/cmake/ThirdPartyNoticesGenerate.cmake"
            "-DCUBE_NOTICES_CONFIG=$<CONFIG>"
            -P "${PROJECT_SOURCE_DIR}/cmake/ThirdPartyNoticesCheck.cmake")
    set_tests_properties(third_party_notices_complete PROPERTIES
        LABELS "licensing"
        FAIL_REGULAR_EXPRESSION "NOTICES TEST FAILED")
endfunction()
