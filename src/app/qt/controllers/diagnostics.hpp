#pragma once

#include <cube/diagnostics/json.hpp>
#include <cube/diagnostics/jsonl_file.hpp>

#include <QObject>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace cube::app {

// The developer diagnostics channel (docs/diagnostics.md): what the running app
// is reading and deciding, written as JSON Lines to a file at a fixed place
// for the current user, so that a scan that misnames a colour or a cube that
// will not assemble can be looked into while the app is still up.
//
// OFF by default, and then it costs a boolean test where a controller would
// have written something. It is switched on and off live - from Settings, or
// the shortcut in DesktopShell.qml - with no restart; the CUBE_DIAGNOSTICS
// environment variable only sets the state the app starts in.
//
// What it writes is numbers and names: Lab values, cluster assignments, the
// cube's state, move lists. Never a camera frame, unless the separate
// framesEnabled switch is also on, and then the frames are written by the
// recorder docs/recording-format.md describes, beside the file.
//
// Everything here runs on the GUI thread, like the controllers that write to
// it.
class Diagnostics : public QObject {
    Q_OBJECT

    // The platform has somewhere to put the file. False on the phones, whose
    // paths are not wired yet (interfaces/paths.hpp).
    Q_PROPERTY(bool available READ available CONSTANT)
    // Records are being written.
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // Camera frames are kept as well - a separate switch, because frames show
    // whatever the camera sees, which can be a person. Only takes effect
    // while enabled.
    Q_PROPERTY(
        bool framesEnabled READ framesEnabled WRITE setFramesEnabled NOTIFY framesEnabledChanged)
    // Where records go, and the file now being written (empty while off).
    Q_PROPERTY(QString directory READ directory CONSTANT)
    Q_PROPERTY(QString path READ path NOTIFY enabledChanged)
    // Where frames are being written, or empty: enabled and framesEnabled.
    Q_PROPERTY(QString framesRoot READ framesRoot NOTIFY framesRootChanged)
    // Why the last attempt to switch it on did not work, or empty.
    Q_PROPERTY(QString error READ error NOTIFY enabledChanged)

public:
    // The per-user place the platform names (interfaces/paths.hpp), and the
    // state CUBE_DIAGNOSTICS and CUBE_DIAGNOSTICS_FRAMES ask for.
    explicit Diagnostics(QObject* parent = nullptr);
    // An explicit directory, and nothing read from the environment: for the
    // tests.
    explicit Diagnostics(std::filesystem::path directory, QObject* parent = nullptr);
    ~Diagnostics() override;

    // The file's name inside directory(). Fixed, so that another process can
    // open it without being told where it is.
    static constexpr std::string_view kFileName = "diagnostics.jsonl";

    [[nodiscard]] bool available() const noexcept { return !directory_.empty(); }
    // Whether call sites should build a record at all. A single test of a
    // bool; nothing is allocated or computed while it is false.
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] bool framesEnabled() const noexcept { return frames_enabled_; }
    [[nodiscard]] QString directory() const;
    [[nodiscard]] QString path() const;
    [[nodiscard]] QString framesRoot() const;
    [[nodiscard]] QString error() const { return error_; }

    void setEnabled(bool enabled);
    void setFramesEnabled(bool enabled);

    // Writes one record of `kind`. Does nothing while no file is open, so a
    // caller that has already tested enabled() need not test it again.
    void write(std::string_view kind, cube::diagnostics::Json data);

    // The full current scan and solve state, as one `snapshot` record, for
    // capturing a bad result at the moment it is on screen. Works while the
    // channel is off, too: the file is opened for the one record and closed
    // again, and nothing else is switched on. snapshotRequested is where the
    // controllers' state is gathered (AppControllers).
    Q_INVOKABLE void snapshot();
    // Switches the channel on or off. For the shortcut.
    Q_INVOKABLE void toggle() { setEnabled(!enabled_); }

signals:
    void enabledChanged();
    void framesEnabledChanged();
    void framesRootChanged();
    // Emitted by snapshot(); whoever knows the state answers by calling
    // write("snapshot", ...).
    void snapshotRequested();

private:
    // Opens the file (moving the last run's aside the first time) and
    // writes the session header. False, and error_ set, when it cannot.
    [[nodiscard]] bool open(std::string_view event);
    [[nodiscard]] std::filesystem::path file_path() const;

    std::filesystem::path directory_;
    cube::diagnostics::JsonlFile file_;
    bool enabled_ = false;
    bool frames_enabled_ = false;
    // The file has been opened in this process: later openings append to it
    // rather than moving the run's earlier records aside.
    bool opened_once_ = false;
    std::uint64_t seq_ = 0;
    QString error_;
};

}  // namespace cube::app
