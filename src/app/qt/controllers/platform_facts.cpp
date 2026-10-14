#include "controllers/platform_facts.hpp"

#include "interfaces/platform_info.hpp"

#include <QString>
#include <QVariantMap>

namespace cube::app {

PlatformFacts::PlatformFacts(QObject* parent) : QObject(parent) {
    const auto info = cube::platform::platform_info::current();
    is_mobile_ = info.is_mobile;
    for (const auto& backend : info.graphics_backends) {
        graphics_backends_.append(QVariantMap{
            {QStringLiteral("key"), QString::fromStdString(backend.key)},
            {QStringLiteral("label"), QString::fromStdString(backend.label)},
        });
    }
    for (const auto& face : info.japanese_faces) {
        japanese_faces_.append(QString::fromStdString(face));
    }
    for (const auto& face : info.chinese_faces) {
        chinese_faces_.append(QString::fromStdString(face));
    }
}

bool PlatformFacts::isMobile() const noexcept {
    return is_mobile_;
}

QVariantList PlatformFacts::graphicsBackends() const {
    return graphics_backends_;
}

QStringList PlatformFacts::japaneseFaces() const {
    return japanese_faces_;
}

QStringList PlatformFacts::chineseFaces() const {
    return chinese_faces_;
}

}  // namespace cube::app
