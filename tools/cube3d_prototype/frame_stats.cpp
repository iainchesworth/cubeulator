#include "frame_stats.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QQuickWindow>
#include <QTextStream>

#include <algorithm>
#include <numeric>

namespace cube3d {

namespace {

[[nodiscard]] double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto at = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    return values.at(at);
}

}  // namespace

FrameStats::FrameStats(QObject* parent) : QObject(parent) {
    process_clock_.start();
}

void FrameStats::attach(QQuickWindow* window) {
    window_ = window;
    connect(window, &QQuickWindow::afterAnimating, this, [this] {
        animated_clock_.start();
        animated_ = true;
    });
    connect(window, &QQuickWindow::frameSwapped, this, [this] {
        if (animated_ && !current_.isEmpty()) {
            phases_[current_].frame_ms.push_back(static_cast<double>(animated_clock_.nsecsElapsed()) /
                                                 1.0e6);
        }
        animated_ = false;
        if (scene_ready_ms_ < 0.0) {
            scene_ready_ms_ = static_cast<double>(process_clock_.nsecsElapsed()) / 1.0e6;
            frame_clock_.start();
            return;
        }
        const double ms = static_cast<double>(frame_clock_.nsecsElapsed()) / 1.0e6;
        frame_clock_.start();
        if (!current_.isEmpty()) {
            phases_[current_].intervals_ms.push_back(ms);
        }
    });
}

void FrameStats::phase(const QString& name) {
    current_ = name;
    if (!name.isEmpty()) {
        phases_[name];
    }
}

double FrameStats::nowMicros() const {
    return static_cast<double>(process_clock_.nsecsElapsed()) / 1000.0;
}

void FrameStats::tally(const QString& name, double micros) {
    Tally& t = tallies_[name];
    t.micros += micros;
    t.worst = std::max(t.worst, micros);
    ++t.count;
}

bool FrameStats::grab(const QString& path) {
    return window_ != nullptr && window_->grabWindow().save(path);
}

void FrameStats::finish(const QVariantMap& facts) {
    QJsonObject out = QJsonObject::fromVariantMap(facts);
    out.insert(QStringLiteral("sceneReadyMs"), scene_ready_ms_);
    for (const auto& [name, phase] : phases_) {
        const auto& v = phase.intervals_ms;
        QJsonObject p;
        p.insert(QStringLiteral("frames"), static_cast<int>(v.size()));
        if (!v.empty()) {
            p.insert(QStringLiteral("meanMs"),
                     std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size()));
            p.insert(QStringLiteral("p50Ms"), percentile(v, 0.50));
            p.insert(QStringLiteral("p95Ms"), percentile(v, 0.95));
            p.insert(QStringLiteral("p99Ms"), percentile(v, 0.99));
            p.insert(QStringLiteral("maxMs"), *std::ranges::max_element(v));
        }
        const auto& f = phase.frame_ms;
        if (!f.empty()) {
            p.insert(QStringLiteral("syncRenderMeanMs"),
                     std::accumulate(f.begin(), f.end(), 0.0) / static_cast<double>(f.size()));
            p.insert(QStringLiteral("syncRenderP95Ms"), percentile(f, 0.95));
            p.insert(QStringLiteral("syncRenderMaxMs"), *std::ranges::max_element(f));
        }
        out.insert(name, p);
    }
    for (const auto& [name, t] : tallies_) {
        QJsonObject p;
        p.insert(QStringLiteral("count"), t.count);
        p.insert(QStringLiteral("meanMicros"), t.count > 0 ? t.micros / t.count : 0.0);
        p.insert(QStringLiteral("worstMicros"), t.worst);
        out.insert(name, p);
    }
    const QByteArray line = QJsonDocument(out).toJson(QJsonDocument::Compact);
    QTextStream(stdout) << line << "\n";
    // Where there is no standard output to read (a phone), the log has it.
    qInfo().noquote() << "CUBE3D-RESULT" << QString::fromUtf8(line);
    if (!report_path_.isEmpty()) {
        QFile file(report_path_);
        if (file.open(QIODevice::Append)) {
            file.write(line + "\n");
        }
    }
    QCoreApplication::quit();
}

}  // namespace cube3d
