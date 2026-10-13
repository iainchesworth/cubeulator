#include "SystemTheme.hpp"

#include <QGuiApplication>
#include <QStyleHints>

SystemTheme::SystemTheme(QObject* parent) : QObject(parent) {
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) { emit darkChanged(); });
}

bool SystemTheme::isDark() const {
    return qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}
