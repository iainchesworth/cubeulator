#include "controllers/frame_image_provider.hpp"

#include <utility>

namespace cube::app {

FrameImageProvider::FrameImageProvider(std::function<cv::Mat()> latest_frame)
    : QQuickImageProvider(QQuickImageProvider::Image), latest_frame_(std::move(latest_frame)) {}

QImage FrameImageProvider::requestImage(const QString& /*id*/, QSize* size,
                                        const QSize& /*requestedSize*/) {
    const cv::Mat frame = latest_frame_();
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
