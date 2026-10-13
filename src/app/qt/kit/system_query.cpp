#include "system_query.hpp"

#include <QtGlobal>

// Apple platforms answer in system_query_apple.mm (Objective-C); every other
// platform is here.
#if !defined(Q_OS_APPLE)

#if defined(Q_OS_WIN)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace cube::app::kit {

double querySystemTextScale() {
    // Settings > Accessibility > Text size. Stored as a percentage, 100 to
    // 225, under the user's own key; absent until the slider is first moved.
    DWORD percent = 100;
    DWORD size = sizeof(percent);
    const LSTATUS status = ::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", L"TextScaleFactor",
                                           RRF_RT_REG_DWORD, nullptr, &percent, &size);
    if (status != ERROR_SUCCESS || percent < 100 || percent > 225) {
        return 1.0;
    }
    return static_cast<double>(percent) / 100.0;
}

bool querySystemReducedMotion() {
    // Settings > Accessibility > Visual effects > Animation effects. The
    // API reports whether animation is ON, so reduced motion is its absence.
    BOOL animations = TRUE;
    if (::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) == 0) {
        return false;
    }
    return animations == FALSE;
}

}  // namespace cube::app::kit

#elif defined(Q_OS_ANDROID)

#include <QCoreApplication>
#include <QJniObject>

namespace cube::app::kit {

namespace {
QJniObject activityContext() {
    return QNativeInterface::QAndroidApplication::context();
}
}  // namespace

double querySystemTextScale() {
    // Settings > Display > Font size, as Configuration.fontScale.
    const QJniObject context = activityContext();
    if (!context.isValid()) {
        return 1.0;
    }
    const QJniObject resources = context.callObjectMethod("getResources", "()Landroid/content/res/Resources;");
    if (!resources.isValid()) {
        return 1.0;
    }
    const QJniObject configuration =
        resources.callObjectMethod("getConfiguration", "()Landroid/content/res/Configuration;");
    if (!configuration.isValid()) {
        return 1.0;
    }
    const jfloat scale = configuration.getField<jfloat>("fontScale");
    return scale > 0.0F ? static_cast<double>(scale) : 1.0;
}

bool querySystemReducedMotion() {
    // "Remove animations" sets the global animator duration scale to zero.
    const QJniObject context = activityContext();
    if (!context.isValid()) {
        return false;
    }
    const QJniObject resolver = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
    if (!resolver.isValid()) {
        return false;
    }
    const QJniObject key = QJniObject::fromString(QStringLiteral("animator_duration_scale"));
    const jfloat scale = QJniObject::callStaticMethod<jfloat>(
        "android/provider/Settings$Global", "getFloat", "(Landroid/content/ContentResolver;Ljava/lang/String;F)F",
        resolver.object(), key.object<jstring>(), 1.0F);
    return scale == 0.0F;
}

}  // namespace cube::app::kit

#elif defined(Q_OS_LINUX)

#include <QProcess>
#include <QString>
#include <QStringList>

namespace cube::app::kit {

namespace {
// GNOME and its derivatives keep both in GSettings. Asking the command-line
// tool avoids a GIO dependency; where it is missing (KDE without it, a
// container) the answer is empty and the neutral value follows.
QString gsettingsInterfaceKey(const QString& key) {
    QProcess process;
    process.start(QStringLiteral("gsettings"),
                  {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"), key});
    if (!process.waitForFinished(250) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        process.kill();
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}
}  // namespace

double querySystemTextScale() {
    bool ok = false;
    const double scale = gsettingsInterfaceKey(QStringLiteral("text-scaling-factor")).toDouble(&ok);
    return ok && scale > 0.0 ? scale : 1.0;
}

bool querySystemReducedMotion() {
    return gsettingsInterfaceKey(QStringLiteral("enable-animations")) == QStringLiteral("false");
}

}  // namespace cube::app::kit

#else

namespace cube::app::kit {
double querySystemTextScale() {
    return 1.0;
}
bool querySystemReducedMotion() {
    return false;
}
}  // namespace cube::app::kit

#endif

#endif  // !Q_OS_APPLE
