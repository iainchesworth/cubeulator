#include "keep_awake.hpp"

#include <QCoreApplication>
#include <QGuiApplication>

#include <utility>

namespace cube::app {

KeepAwake::KeepAwake(QObject* parent)
    : KeepAwake(cube::platform::desktop_shell::make_keep_awake_backend(), parent) {}

KeepAwake::KeepAwake(std::unique_ptr<KeepAwakeBackend> backend, QObject* parent)
    : QObject(parent), backend_(std::move(backend)) {
    if (backend_) {
        backend_->setReporter([this](bool held) { setHeld(held && requested_); });
    }
    // "In front" is a GUI notion; a QCoreApplication (the unit tests) has no
    // application state and counts as being in front.
    if (const auto* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(gui, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState) { sync(); });
    }
}

KeepAwake::~KeepAwake() {
    if (backend_ && requested_) {
        backend_->release();
    }
}

void KeepAwake::setActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    emit activeChanged();
    sync();
}

bool KeepAwake::appInFront() {
    const auto* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    return gui == nullptr || QGuiApplication::applicationState() == Qt::ApplicationActive;
}

void KeepAwake::sync() {
    const bool want = active_ && appInFront();
    if (want == requested_ || !backend_) {
        return;
    }
    requested_ = want;
    if (want) {
        backend_->acquire();
    } else {
        backend_->release();
        setHeld(false);
    }
}

void KeepAwake::setHeld(bool held) {
    if (held_ == held) {
        return;
    }
    held_ = held;
    emit heldChanged();
}

}  // namespace cube::app
