#pragma once

#include <QQuickImageProvider>

namespace cube::app {

class ScanSession;

// Serves image://scanFrame/latest as a QImage converted from ScanSession's
// most recent BGR frame. Registered in main.cpp via
// QQmlApplicationEngine::addImageProvider - not QML-visible as a property
// itself, unlike the QObject controllers.
class FrameImageProvider : public QQuickImageProvider {
public:
    explicit FrameImageProvider(ScanSession& session);

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    ScanSession& session_;
};

}  // namespace cube::app
