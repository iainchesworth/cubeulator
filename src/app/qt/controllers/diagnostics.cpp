#include "controllers/diagnostics.hpp"

#include "interfaces/paths.hpp"

#include <cube/diagnostics/record.hpp>
#include <cube/version.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QtGlobal>

#include <chrono>
#include <string>
#include <utility>

namespace cube::app {

namespace {

// 1, true, on or yes, in any case: how a developer says "yes" to an
// environment variable.
[[nodiscard]] bool wants(const char* variable) {
    const QString value = qEnvironmentVariable(variable).trimmed().toLower();
    return value == QLatin1String("1") || value == QLatin1String("true") ||
           value == QLatin1String("on") || value == QLatin1String("yes");
}

// UTF-16 and back, so that a user name with an accent in it is the same
// characters here as on disk.
[[nodiscard]] QString shown(const std::filesystem::path& path) {
    return QDir::toNativeSeparators(QString::fromStdU16String(path.u16string()));
}

}  // namespace

Diagnostics::Diagnostics(QObject* parent)
    : Diagnostics(cube::platform::paths::current().diagnostics_dir, parent) {
    // The environment only sets the state the app starts in. After that the
    // switch is the one in the app, which is the point of there being one.
    frames_enabled_ = wants("CUBE_DIAGNOSTICS_FRAMES");
    if (wants("CUBE_DIAGNOSTICS")) {
        setEnabled(true);
    }
}

Diagnostics::Diagnostics(std::filesystem::path directory, QObject* parent)
    : QObject(parent), directory_(std::move(directory)) {}

Diagnostics::~Diagnostics() {
    if (enabled_) {
        write("session", cube::diagnostics::Json::object().set("event", "off"));
    }
}

QString Diagnostics::directory() const {
    return shown(directory_);
}

QString Diagnostics::path() const {
    return enabled_ ? shown(file_path()) : QString();
}

QString Diagnostics::framesRoot() const {
    if (!enabled_ || !frames_enabled_ || !available()) {
        return {};
    }
    return shown(directory_ / "frames");
}

std::filesystem::path Diagnostics::file_path() const {
    return directory_ / std::string(kFileName);
}

bool Diagnostics::open(std::string_view event) {
    error_.clear();
    if (!available()) {
        // Nothing to report: the screens do not offer the switch (available).
        return false;
    }
    if (const auto problem =
            file_.open(file_path(), cube::diagnostics::JsonlFile::kDefaultMaxBytes, !opened_once_);
        problem.has_value()) {
        error_ = QString::fromStdString(*problem);
        qWarning("Diagnostics: not writing: %s", problem->c_str());
        return false;
    }
    opened_once_ = true;

    // The header says who wrote what follows, so that a file is readable on
    // its own.
    cube::diagnostics::Json session = cube::diagnostics::Json::object();
    session.set("event", event);
    session.set("pid", static_cast<std::int64_t>(QCoreApplication::applicationPid()));
    session.set("version", cube::version_string);
    session.set("commit", cube::git_commit);
    session.set("build_target", cube::build_target);
    session.set("directory", directory().toStdString());
    session.set("frames", frames_enabled_);
    write("session", std::move(session));
    return true;
}

void Diagnostics::setEnabled(bool enabled) {
    if (enabled == enabled_) {
        return;
    }
    if (enabled) {
        if (!open("on")) {
            emit enabledChanged();
            return;
        }
        enabled_ = true;
    } else {
        write("session", cube::diagnostics::Json::object().set("event", "off"));
        file_.close();
        enabled_ = false;
    }
    emit enabledChanged();
    emit framesRootChanged();
}

void Diagnostics::setFramesEnabled(bool enabled) {
    if (enabled == frames_enabled_) {
        return;
    }
    frames_enabled_ = enabled;
    if (enabled_) {
        write("session", cube::diagnostics::Json::object()
                             .set("event", enabled ? "frames_on" : "frames_off")
                             .set("frames_root", framesRoot().toStdString()));
    }
    emit framesEnabledChanged();
    emit framesRootChanged();
}

void Diagnostics::write(std::string_view kind, cube::diagnostics::Json data) {
    if (!file_.is_open()) {
        return;
    }
    const std::string line =
        cube::diagnostics::make_record(
            {.seq = seq_++,
             .time = cube::diagnostics::utc_timestamp(std::chrono::system_clock::now()),
             .kind = kind},
            std::move(data))
            .dump();
    if (!file_.write_line(line)) {
        qWarning("Diagnostics: could not write a record");
    }
}

void Diagnostics::snapshot() {
    const bool was_enabled = enabled_;
    if (!was_enabled && !open("snapshot")) {
        emit enabledChanged();
        return;
    }
    emit snapshotRequested();
    if (!was_enabled) {
        file_.close();
    }
}

}  // namespace cube::app
