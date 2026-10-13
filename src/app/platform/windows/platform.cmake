# Windows. Included by cmake/Platform.cmake; the contract it fills in is
# described at the top of that file.

set(CUBEULATOR_PLATFORM_DISPLAY_NAME "Windows")
set(CUBEULATOR_PLATFORM_DESKTOP ON)

# Windows keeps its own platform plugin: it is the only one deployed beside
# the binaries here, and it needs no display for the test binaries either.
set(CUBEULATOR_PLATFORM_TEST_QPA_PLATFORM "")

# A Windows session is always there.
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_VARIABLES "")
set(CUBEULATOR_PLATFORM_TEST_DISPLAY_HELP "")

set(CUBEULATOR_PLATFORM_QT_SEARCH_ROOTS "C:/Qt" "$ENV{USERPROFILE}/Qt")
set(CUBEULATOR_PLATFORM_QT_ARCH_DIRS msvc2022_64 msvc2019_64 mingw_64)

# The installer's root: the licence and the notices sit beside bin/.
set(CUBEULATOR_PLATFORM_NOTICES_DIR ".")

set(CUBEULATOR_PLATFORM_SOURCES
    platform/windows/camera_windows.cpp
    platform/windows/capture_thread_scope_windows.cpp
    platform/windows/crash_handler_windows.cpp
    platform/windows/inference_env_windows.cpp
    platform/windows/paths_windows.cpp
    platform/windows/platform_info_windows.cpp)

# camera_windows.cpp enumerates via native DirectShow device enumeration
# (ICreateDevEnum/IEnumMoniker) rather than OpenCvCaptureLoop's generic
# probe - see its own comment for why, and for why DirectShow rather than
# Media Foundation. strmiids provides the CLSID_SystemDeviceEnum/
# CLSID_VideoInputDeviceCategory/IID_ICreateDevEnum GUID definitions; ole32
# provides CoCreateInstance/CoInitializeEx; oleaut32 provides the
# VARIANT/BSTR helpers (VariantInit/VariantClear) used to read each
# device's friendly name. DbgHelp backs crash_handler_windows.cpp's
# unhandled-exception filter, which writes a minidump for the hard faults
# (access violation, stack overflow) that no C++ catch can see under MSVC's
# default /EHsc.
set(CUBEULATOR_PLATFORM_LINK_LIBRARIES strmiids ole32 oleaut32 dbghelp)
set(CUBEULATOR_PLATFORM_QT_LINK_LIBRARIES "")

# std::getenv is flagged "unsafe" by the MSVC CRT; the usage is correct and
# portable (the suggested _dupenv_s alternative is Windows-only), and this
# build treats warnings as errors. Applies under clang-cl too, which uses the
# same CRT headers.
set(CUBEULATOR_PLATFORM_COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)

set(CUBEULATOR_PLATFORM_APP_PROPERTIES "")

function(cube_platform_prepare_dependencies)
endfunction()

# Qt's DLLs aren't next to cube_app.exe or on PATH by default, so it fails to
# start (STATUS_DLL_NOT_FOUND) unless launched from a shell that already has
# Qt's bin dir on PATH - which Visual Studio's F5/"Select Startup Item" does
# not. windeployqt copies everything cube_app.exe actually needs (Qt DLLs,
# platform/style plugins, translations) next to it after every build, so it
# runs standalone regardless of how it's launched.
function(cube_platform_deploy_qt_runtime target qml_dir)
    find_program(CUBEULATOR_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt6 windeployqt
        HINTS "${Qt6_DIR}/../../../bin")
    if(CUBEULATOR_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CUBEULATOR_WINDEPLOYQT_EXECUTABLE}"
                --qmldir "${qml_dir}"
                "$<TARGET_FILE:${target}>"
            COMMENT "windeployqt: copying Qt runtime next to $<TARGET_FILE_NAME:${target}>")
    else()
        message(WARNING
            "windeployqt not found near Qt6_DIR (${Qt6_DIR}); "
            "${target}.exe will need Qt's bin dir on PATH to run standalone.")
    endif()
endfunction()

# What windeployqt does not copy for the app because the app never needs it
# (QtTest, which the system tests use to send input events). Without it the
# binary dies at start-up with STATUS_DLL_NOT_FOUND.
function(cube_platform_deploy_qt_library target qt_target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${qt_target}>" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "copying $<TARGET_FILE_NAME:${qt_target}> next to $<TARGET_FILE_NAME:${target}>")
endfunction()

# The NSIS installer: Start menu entry, uninstaller, version, icon.
# packaging/windows/Packaging.cmake.
macro(cube_platform_configure_packaging)
    include("${PROJECT_SOURCE_DIR}/packaging/windows/Packaging.cmake")
endmacro()

# The shell's Windows implementation: a display-required execution state and
# the AppUserModelID. SetCurrentProcessExplicitAppUserModelID lives in shell32.
function(cube_platform_configure_desktop_shell target)
    target_sources(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/app/platform/windows/desktop_shell_windows.cpp")
    target_link_libraries(${target} PRIVATE shell32)
endfunction()

# Cubeulator.exe with its version resource and icon, and install rules that
# carry the vcpkg DLLs, Qt and the C++ runtime beside it.
# packaging/windows/ConfigureApp.cmake.
function(cube_platform_configure_desktop_app target)
    include("${PROJECT_SOURCE_DIR}/packaging/windows/ConfigureApp.cmake")
endfunction()
