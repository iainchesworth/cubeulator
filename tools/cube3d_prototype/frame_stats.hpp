#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QQuickWindow>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <map>
#include <vector>

namespace cube3d {

// What the frames cost, in phases: the time between one frame being swapped in
// and the next, collected for as long as a phase is set. With the render loop
// pinned to `basic` and vsync off (main.cpp does both, unless asked not to) the
// interval is how long the scene graph takes to prepare and submit a frame, so
// a frame that is too long shows as a long interval and a cheap scene as a
// short one.
class FrameStats : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("made in C++")

public:
    explicit FrameStats(QObject* parent = nullptr);

    // Starts counting the frames of `window`. The first frame shown sets
    // sceneReadyMs.
    Q_INVOKABLE void attach(QQuickWindow* window);
    // The frames from now until the next call are those of phase `name`.
    Q_INVOKABLE void phase(const QString& name);
    Q_INVOKABLE double nowMicros() const;
    // Adds a duration to the named tally (a count and a total, microseconds).
    Q_INVOKABLE void tally(const QString& name, double micros);
    // Saves a picture of the window to `path`.
    Q_INVOKABLE bool grab(const QString& path);
    // Prints what was measured, with `facts` (mode, size...) beside it, to
    // standard output and, when a path was given, appends it to that file as one
    // line of JSON. Then quits.
    Q_INVOKABLE void finish(const QVariantMap& facts);

    void setReportPath(QString path) { report_path_ = std::move(path); }

private:
    struct Phase {
        std::vector<double> intervals_ms;
        // From the animations having advanced to the frame being swapped in:
        // what the scene graph itself costs, which the interval above (set by
        // the animation timer, not by the work) does not show.
        std::vector<double> frame_ms;
    };
    struct Tally {
        double micros = 0.0;
        double worst = 0.0;
        int count = 0;
    };

    QQuickWindow* window_ = nullptr;
    QElapsedTimer process_clock_;
    QElapsedTimer frame_clock_;
    QString current_;
    QElapsedTimer animated_clock_;
    bool animated_ = false;
    std::map<QString, Phase> phases_;
    std::map<QString, Tally> tallies_;
    double scene_ready_ms_ = -1.0;
    QString report_path_;
};

}  // namespace cube3d
