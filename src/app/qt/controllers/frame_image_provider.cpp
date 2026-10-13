#include "controllers/frame_image_provider.hpp"

#include "controllers/scan_session.hpp"

namespace cube::app {

FrameImageProvider::FrameImageProvider(ScanSession& session)
    : QQuickImageProvider(QQuickImageProvider::Image), session_(session) {}

QImage FrameImageProvider::requestImage(const QString& /*id*/, QSize* size,
                                        const QSize& /*requestedSize*/) {
    const cv::Mat frame = session_.latestFrame();
    if (frame.empty()) {
        if (size) {
            *size = QSize(0, 0);
        }
        return {};
    }

    if (size) {
        *size = QSize(frame.cols, frame.rows);
    }
    // QImage doesn't take ownership of frame's buffer, so copy() before the
    // local cv::Mat goes out of scope.
    return QImage(frame.data, frame.cols, frame.rows, static_cast<int>(frame.step),
                  QImage::Format_BGR888)
        .copy();
}

}  // namespace cube::app
