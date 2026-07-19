#include "interfaces/camera.hpp"
#include "platform/detail/opencv_capture_loop.hpp"

#include <mfapi.h>
#include <mfidl.h>
#include <objbase.h>

namespace cube::platform::camera {

namespace {

detail::OpenCvCaptureLoop& loop() {
    static detail::OpenCvCaptureLoop instance(cv::CAP_MSMF);
    return instance;
}

}  // namespace

// Native Media Foundation enumeration rather than OpenCvCaptureLoop's
// generic open-a-cv::VideoCapture-and-check-isOpened() probe (still used
// by the Linux/macOS platform files): MFEnumDeviceSources queries the
// driver's registered device list without activating/streaming from any
// of them, unlike opening a cv::VideoCapture per candidate index.
//
// That distinction matters here, not just as a performance nicety: opening
// a real camera via MSMF (with D3D11 hardware acceleration negotiation),
// then closing it again moments before start() reopens the very same
// index, reliably reproduced multi-second stalls and process crashes on
// real hardware - confirmed via cap_msmf's own internal logging, which
// showed the SourceReaderCB teardown for the real device taking 14-24s
// longer than for an empty index, with the process dying shortly after
// start() reopened it. MFEnumDeviceSources sidesteps the double-open
// entirely rather than working around the driver's teardown/reopen race.
Result<std::vector<DeviceInfo>> enumerate_devices() {
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        return std::unexpected(CubeError::camera_not_found);
    }

    IMFAttributes* attributes = nullptr;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (SUCCEEDED(hr)) {
        hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    }

    IMFActivate** raw_devices = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(hr)) {
        hr = MFEnumDeviceSources(attributes, &raw_devices, &count);
    }
    if (attributes != nullptr) {
        attributes->Release();
    }

    std::vector<DeviceInfo> devices;
    if (SUCCEEDED(hr)) {
        for (UINT32 i = 0; i < count; ++i) {
            devices.push_back(DeviceInfo{.identifier = std::to_string(i),
                                          .display_name = "Camera " + std::to_string(i)});
            if (raw_devices[i] != nullptr) {
                raw_devices[i]->Release();
            }
        }
    }
    if (raw_devices != nullptr) {
        CoTaskMemFree(raw_devices);
    }

    MFShutdown();

    if (FAILED(hr)) {
        return std::unexpected(CubeError::camera_not_found);
    }
    return devices;
}

Result<void> start(const DeviceInfo& device) {
    return loop().start(device);
}

void stop() {
    loop().stop();
}

std::optional<cv::Mat> try_pop_latest_frame() {
    return loop().try_pop_latest_frame();
}

bool is_capturing() noexcept {
    return loop().is_capturing();
}

}  // namespace cube::platform::camera
