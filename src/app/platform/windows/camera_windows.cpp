#include "interfaces/camera.hpp"
#include "platform/detail/capture_thread_scope.hpp"
#include "platform/detail/opencv_capture_loop.hpp"

#include <dshow.h>
#include <objbase.h>

#include <cstddef>
#include <string>

namespace cube::platform::camera {

namespace {

std::string bstr_to_utf8(BSTR bstr) {
    if (bstr == nullptr) {
        return {};
    }
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, bstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    // len includes room for the trailing null; the std::string's own
    // buffer already reserves that via its own null terminator.
    std::string out(static_cast<std::size_t>(len - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, bstr, -1, out.data(), len, nullptr, nullptr);
    return out;
}

detail::OpenCvCaptureLoop& loop() {
    // DSHOW, not MSMF: measured on real hardware (Logitech StreamCam),
    // cv::VideoCapture::open()+first frame via CAP_MSMF took ~9-10s cold,
    // consistently, across repeated clean-process trials - not the
    // close/reopen race this file already works around below, just the
    // plain first open of a session. Neither disabling
    // CAP_PROP_HW_ACCELERATION nor pre-selecting a smaller resolution (both
    // plausible-looking fixes) helped; both made MSMF's negotiation slower
    // still (up to ~33s). CAP_DSHOW opened the same device, cold, in
    // ~1.0-1.4s open+first-frame - repeatably, on the same hardware.
    // DirectShow lacks MSMF's async SourceReaderCB/D3D11 topology
    // negotiation entirely, which is apparently where a UVC extension-unit-
    // heavy device like this one pays most of its MSMF activation cost.
    // enumerate_devices() below was switched to DirectShow's own device
    // enumerator alongside this so the index space stays consistent
    // between "which device the Settings list shows" and "which device
    // start() actually opens" - see its own comment.
    // Photometry::supported: DirectShow is also the reason the photometry
    // lock (see interfaces/camera.hpp) can exist at all - it maps camera
    // properties onto IAMVideoProcAmp (brightness/saturation/white balance)
    // and IAMCameraControl (exposure), and the hardware probe in
    // tests/integration/camera_property_probe_test.cpp verified against this
    // very backend that exposure, brightness and saturation writes reach the
    // sensor (monotone response curves) and that auto white balance
    // round-trips 0/1. This is the ONLY backend given that flag, because it
    // is the only one anyone has measured.
    static detail::OpenCvCaptureLoop instance(detail::capture_backend(),
                                              detail::OpenCvCaptureLoop::Photometry::supported);
    return instance;
}

}  // namespace

namespace detail {

// See loop() above for why this is DirectShow and not Media Foundation.
cv::VideoCaptureAPIs capture_backend() noexcept {
    return cv::CAP_DSHOW;
}

const char* capture_backend_name() noexcept {
    return "CAP_DSHOW";
}

}  // namespace detail

// Walks the SAME DirectShow video-capture-device enumeration that OpenCV's
// CAP_DSHOW backend (see loop() above) resolves cv::VideoCapture::open's
// index argument against: CLSID_SystemDeviceEnum's
// CreateClassEnumerator(CLSID_VideoInputDeviceCategory) -> IEnumMoniker, in
// registration order. This used to walk Media Foundation's
// MFEnumDeviceSources instead - lighter-weight, and fine while start() also
// opened through MSMF - but now that start() opens through DSHOW (see
// loop()), enumerating through a DIFFERENT device-listing API would risk an
// index mismatch on a machine where the two device trees disagree on
// order: the Settings screen could label a camera "Camera 1" while start()
// actually opens whatever DirectShow considers device 1, a different
// physical camera. Keeping both on DirectShow's own listing closes that
// gap. Like MFEnumDeviceSources, CreateClassEnumerator only walks the
// registered device list - it never activates/streams from a candidate -
// so this stays a cheap, hardware-safe probe, not the "open every index to
// see what answers" probe the generic (Linux/macOS) fallback in
// opencv_capture_loop.cpp uses.
Result<std::vector<DeviceInfo>> enumerate_devices() {
    // This runs on a Qt thread-pool worker (see CameraController), which
    // carries no COM initialization of its own; the scope supplies the MTA
    // one the capture loop uses too, for the same reason - no message pump
    // on this thread to service STA marshaling.
    const detail::CaptureThreadScope capture_thread_scope;

    ICreateDevEnum* dev_enum = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ICreateDevEnum, reinterpret_cast<void**>(&dev_enum));
    if (FAILED(hr)) {
        return std::unexpected(CubeError::camera_not_found);
    }

    IEnumMoniker* enum_moniker = nullptr;
    hr = dev_enum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enum_moniker, 0);
    dev_enum->Release();

    std::vector<DeviceInfo> devices;
    // CreateClassEnumerator returns S_FALSE (and a null enum_moniker) when
    // the category is registered but has zero devices - not a failure,
    // just "no cameras", same empty-list outcome as a real error below.
    if (hr != S_OK || enum_moniker == nullptr) {
        return devices;
    }

    IMoniker* moniker = nullptr;
    int index = 0;
    while (enum_moniker->Next(1, &moniker, nullptr) == S_OK) {
        std::string display_name = "Camera " + std::to_string(index);

        IPropertyBag* property_bag = nullptr;
        if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag,
                                             reinterpret_cast<void**>(&property_bag)))) {
            VARIANT name;
            VariantInit(&name);
            if (SUCCEEDED(property_bag->Read(L"FriendlyName", &name, nullptr)) &&
                name.vt == VT_BSTR) {
                display_name = bstr_to_utf8(name.bstrVal);
            }
            VariantClear(&name);
            property_bag->Release();
        }

        // facing stays at its unknown default: desktop webcams face the
        // user, and unknown already gets the selfie-mirror treatment (see
        // Facing in interfaces/camera.hpp).
        devices.push_back(
            DeviceInfo{.identifier = std::to_string(index), .display_name = display_name});

        moniker->Release();
        ++index;
    }
    enum_moniker->Release();

    return devices;
}

Result<void> start(const DeviceInfo& device) {
    return loop().start(device);
}

void stop() {
    loop().stop();
}

std::optional<TimedFrame> try_pop_latest_frame() {
    return loop().try_pop_latest_frame();
}

bool is_capturing() noexcept {
    return loop().is_capturing();
}

void open_permission_settings() {}

// Forwarded to the shared capture loop, which owns the device and the
// capture thread - see interfaces/camera.hpp for what a lock is for and
// what this backend was measured to support.
bool photometry_control_supported() noexcept {
    return loop().photometry_supported();
}

Result<void> request_photometry_lock(const PhotometryLockRequest& request) {
    return loop().request_photometry_lock(request);
}

Result<void> request_photometry_restore() {
    return loop().request_photometry_restore();
}

std::optional<PhotometryLockReport> take_photometry_report() {
    return loop().take_photometry_report();
}

}  // namespace cube::platform::camera
