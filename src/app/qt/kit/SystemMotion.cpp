#include "SystemMotion.hpp"

#include "SystemChangeWatcher.hpp"
#include "system_query.hpp"

SystemMotion::SystemMotion(QObject* parent) : QObject(parent), m_system(cube::app::kit::querySystemReducedMotion()) {
    if (SystemChangeWatcher* watcher = SystemChangeWatcher::instance(); watcher != nullptr) {
        connect(watcher, &SystemChangeWatcher::changed, this, &SystemMotion::refresh);
    }
}

bool SystemMotion::reduced() const {
    return m_hasOverride ? m_override : m_system;
}

bool SystemMotion::isOverridden() const {
    return m_hasOverride;
}

void SystemMotion::refresh() {
    const bool before = reduced();
    m_system = cube::app::kit::querySystemReducedMotion();
    if (before != reduced()) {
        emit reducedChanged();
    }
}

void SystemMotion::setOverride(bool reducedValue) {
    const bool before = reduced();
    const bool hadOverride = m_hasOverride;
    m_hasOverride = true;
    m_override = reducedValue;
    if (before != reduced() || !hadOverride) {
        emit reducedChanged();
    }
}

void SystemMotion::clearOverride() {
    if (!m_hasOverride) {
        return;
    }
    m_hasOverride = false;
    emit reducedChanged();
}
