#include "SystemTextScale.hpp"

#include "SystemChangeWatcher.hpp"
#include "system_query.hpp"

#include <QtMath>

SystemTextScale::SystemTextScale(QObject* parent) : QObject(parent), m_system(cube::app::kit::querySystemTextScale()) {
    if (SystemChangeWatcher* watcher = SystemChangeWatcher::instance(); watcher != nullptr) {
        connect(watcher, &SystemChangeWatcher::changed, this, &SystemTextScale::refresh);
    }
}

qreal SystemTextScale::scale() const {
    return m_override > 0.0 ? m_override : m_system;
}

bool SystemTextScale::isOverridden() const {
    return m_override > 0.0;
}

void SystemTextScale::refresh() {
    const qreal before = scale();
    m_system = cube::app::kit::querySystemTextScale();
    if (!qFuzzyCompare(before, scale())) {
        emit scaleChanged();
    }
}

void SystemTextScale::setOverride(qreal scale) {
    const qreal valueBefore = this->scale();
    const bool overriddenBefore = isOverridden();
    m_override = scale > 0.0 ? scale : 0.0;
    if (!qFuzzyCompare(valueBefore, this->scale()) || overriddenBefore != isOverridden()) {
        emit scaleChanged();
    }
}

void SystemTextScale::clearOverride() {
    setOverride(0.0);
}
