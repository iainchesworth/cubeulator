#pragma once

#include <QQuickImageProvider>

#include <opencv2/core.hpp>

#include <functional>

namespace cube::app {

// Serves image://scanFrame/latest as a QImage converted from the scan
// controller's most recent BGR frame. Registered in main.cpp via
// QQmlApplicationEngine::addImageProvider - not QML-visible as a property
// itself, unlike the QObject controllers.
class FrameImageProvider : public QQuickImageProvider {
public:
    // `latest_frame` is called from the image-loading thread and must be
    // safe to call from there.
    explicit FrameImageProvider(std::function<cv::Mat()> latest_frame);

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    std::function<cv::Mat()> latest_frame_;
};

}  // namespace cube::app
