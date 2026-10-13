#include "interfaces/crash_handler.hpp"

namespace cube::platform::crash_handler {

// Nothing to install. A hard fault is not hidden from a POSIX-family
// process the way it is from MSVC's catch(...): the kernel's core dump, the
// Android tombstone and Apple's crash reports each record the faulting
// address and the process's state without any help from the app.
void install() noexcept {}

}  // namespace cube::platform::crash_handler
