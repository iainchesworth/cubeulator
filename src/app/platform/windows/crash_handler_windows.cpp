#include "interfaces/crash_handler.hpp"

#include <windows.h>
// dbghelp.h requires windows.h first.
#include <dbghelp.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <print>

namespace cube::platform::crash_handler {

namespace {

// Last-resort crash reporting. A hard fault (access violation, stack
// overflow) is a STRUCTURED exception, not a C++ one: MSVC's catch(...)
// under the default /EHsc does not see it, so the try/catch guarding the
// analysis worker cannot report it and the process dies silently - no
// dialog text worth reading, and Windows Error Reporting produced no entry
// at all on this machine, leaving a mid-scan crash with literally zero
// evidence. This filter runs for ANY unhandled fault, writes a minidump
// beside the executable, and prints the exception code and faulting
// address, so the next crash identifies itself instead of being argued
// about. Inert unless the process actually dies.
LONG WINAPI writeCrashDump(EXCEPTION_POINTERS* exception) {
    char dump_path[MAX_PATH] = {};
    ::GetModuleFileNameA(nullptr, dump_path, MAX_PATH);
    // Replace the trailing "cube_app.exe" with the dump name; on failure the
    // path stays as-is and the dump lands next to the binary regardless.
    if (char* last_slash = std::strrchr(dump_path, '\\'); last_slash != nullptr) {
        std::snprintf(last_slash + 1,
                      static_cast<std::size_t>(MAX_PATH - (last_slash + 1 - dump_path)),
                      "cube_app_crash.dmp");
    }

    const HANDLE file = ::CreateFileA(dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = ::GetCurrentThreadId();
        info.ExceptionPointers = exception;
        info.ClientPointers = FALSE;
        // WithIndirectlyReferencedMemory keeps the dump small while still
        // capturing the objects the faulting frames point at - enough to see
        // which container/optional was empty.
        ::MiniDumpWriteDump(
            ::GetCurrentProcess(), ::GetCurrentProcessId(), file,
            static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
            &info, nullptr, nullptr);
        ::CloseHandle(file);
    }

    if (exception != nullptr && exception->ExceptionRecord != nullptr) {
        std::println(stderr, "CUBE CRASH: code=0x{:08X} address={} dump={}",
                     exception->ExceptionRecord->ExceptionCode,
                     exception->ExceptionRecord->ExceptionAddress, dump_path);
        std::fflush(stderr);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void install() noexcept {
    ::SetUnhandledExceptionFilter(writeCrashDump);
}

}  // namespace cube::platform::crash_handler
