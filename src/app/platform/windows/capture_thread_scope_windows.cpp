#include "platform/detail/capture_thread_scope.hpp"

#include <objbase.h>

namespace cube::platform::camera::detail {

// The Windows-only cv::VideoCaptureAPIs backends (MSMF/DSHOW) require COM
// initialized on whichever thread calls into them - not something OpenCV can
// do on our behalf, since it doesn't control thread creation. The GUI thread
// gets this for free from Qt's own startup, but enumerate_devices() is
// deliberately run off it (via QtConcurrent, to avoid freezing the UI for
// the ~15-20s a real hardware probe takes) on a Qt thread-pool thread that
// has no COM initialization of its own.
//
// MTA (COINIT_MULTITHREADED), not STA: MSMF's cap_msmf.cpp delivers frames
// through an async IMFSourceReader callback (SourceReaderCB in its own
// logs) whose completion needs to be marshaled back to the calling
// thread's COM apartment. STA marshaling relies on that thread pumping a
// Windows message loop to receive it - which a QtConcurrent worker thread
// never does - so releasing a real (non-empty-slot) capture opened under
// STA on such a thread reliably stalled for ~14s per device (confirmed
// via cap_msmf's own SourceReaderCB teardown log timestamps) until
// whatever internal timeout eventually recovered it, and the process
// crashed shortly after. MTA doesn't need message-pump-based marshaling
// for calls within the same apartment, which covers this case.
CaptureThreadScope::CaptureThreadScope() noexcept
    : state_(static_cast<std::int32_t>(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}

CaptureThreadScope::~CaptureThreadScope() {
    // Balance only a successful CoInitializeEx: S_FALSE (already
    // initialized on this thread) is a success that still takes a reference.
    if (SUCCEEDED(static_cast<HRESULT>(state_))) {
        CoUninitialize();
    }
}

}  // namespace cube::platform::camera::detail
