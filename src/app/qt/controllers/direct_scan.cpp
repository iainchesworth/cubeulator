#include "controllers/direct_scan.hpp"

#include "controllers/diagnostic_records.hpp"
#include "controllers/diagnostics.hpp"
#include "controllers/scan_reading.hpp"
#include "interfaces/camera.hpp"

#include <cube/diagnostics/describe.hpp>
#include <cube/profiling.hpp>
#include <cube/vision/sticker_colour.hpp>

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QPointF>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <string>
#include <utility>

namespace cube::app {

namespace {

// ~30 Hz: every camera frame is looked at, and a read is a few milliseconds.
constexpr int kPollIntervalMs = 33;

// How long to wait for the camera to report its lock before scanning anyway.
// The lock normally reports within about 40 frames of the camera starting;
// a search for the white balance takes up to about 130 more.
constexpr int kLockWaitFrames = 240;

[[nodiscard]] QString colour_string(const cv::Vec3f& lab) {
    const cv::Vec3b bgr = cube::vision::lab_to_bgr(lab);
    return QColor(bgr[2], bgr[1], bgr[0]).name();
}

// The colours of the corner the scan would like to see, as the names the
// screen looks up (settlingCorner).
[[nodiscard]] QStringList settling_corner_of(const cube::vision::ScanDiagnosis& diagnosis) {
    QStringList names;
    if (!diagnosis.assembly.has_value() || !diagnosis.assembly->settling_corner.has_value()) {
        return names;
    }
    for (const cube::solver::Color colour : *diagnosis.assembly->settling_corner) {
        switch (colour) {
            case cube::solver::Color::White:
                names.append(QStringLiteral("white"));
                break;
            case cube::solver::Color::Yellow:
                names.append(QStringLiteral("yellow"));
                break;
            case cube::solver::Color::Green:
                names.append(QStringLiteral("green"));
                break;
            case cube::solver::Color::Blue:
                names.append(QStringLiteral("blue"));
                break;
            case cube::solver::Color::Orange:
                names.append(QStringLiteral("orange"));
                break;
            case cube::solver::Color::Red:
                names.append(QStringLiteral("red"));
                break;
        }
    }
    return names;
}

[[nodiscard]] bool same_sides(const std::vector<cube::vision::SideScan::Side>& a,
                              const std::vector<cube::vision::SideScan::Side>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].in_view != b[i].in_view || a[i].lab != b[i].lab) {
            return false;
        }
    }
    return true;
}

// What an evaluation came to, in a few dozen characters: how many sides, how
// the 54 stickers were named, how the cube was put together, and the verdict.
// Two evaluations with the same digest tell a reader nothing new - the fused
// colours drift a little with every read of a side held in view - so only a
// change is written.
[[nodiscard]] std::string digest_of(const cube::vision::ScanDiagnosis& diagnosis) {
    using cube::diagnostics::code_of;
    std::string digest = std::to_string(diagnosis.sides.size());
    digest += '/';
    digest += std::to_string(diagnosis.captured);
    if (diagnosis.naming.has_value()) {
        digest += '|';
        digest += std::to_string(static_cast<int>(diagnosis.naming->hue_override));
        for (const auto& side : diagnosis.naming->faces) {
            for (const cube::model::Facelet& facelet : side) {
                digest += static_cast<char>('a' + static_cast<int>(facelet.color));
            }
        }
    }
    if (diagnosis.assembly.has_value()) {
        const cube::vision::AssemblyTrace& trace = *diagnosis.assembly;
        digest += '|';
        digest += std::to_string(static_cast<int>(trace.path));
        digest += ',' + std::to_string(trace.candidates);
        digest += ',' + std::to_string(trace.chosen.value_or(99));
        if (trace.exchange.has_value()) {
            digest += ',' + std::to_string(trace.exchange->first_side);
            digest += ',' + std::to_string(trace.exchange->second_side);
        }
    }
    digest += '|';
    digest += diagnosis.result.has_value() ? std::string("ok")
                                           : std::string(code_of(diagnosis.result.error()));
    return digest;
}

}  // namespace

DirectScan::DirectScan(QObject* parent)
    : QObject(parent), recording_root_(qEnvironmentVariable("CUBE_SCAN_RECORD_DIR")) {
    // For a developer, to scan and record a cube that is not a 3x3 (see defaultSize).
    const int asked = qEnvironmentVariableIntValue("CUBE_SCAN_SIZE");
    if (asked >= 2 && asked <= 9) {
        default_size_ = asked;
        size_ = asked;
        scan_ = cube::vision::SideScan(asked);
    }
    poll_timer_.setInterval(kPollIntervalMs);
    connect(&poll_timer_, &QTimer::timeout, this, &DirectScan::poll);
    connect(&start_watcher_, &QFutureWatcher<cube::Result<void>>::finished, this,
            &DirectScan::handleStartFinished);
}

DirectScan::~DirectScan() {
    finishRecording();
    start_watcher_.waitForFinished();
    if (running_) {
        cube::platform::camera::stop();
    }
}

void DirectScan::setRecordingRoot(const QString& root) {
    if (root == recording_root_) {
        return;
    }
    finishRecording();
    recording_root_ = root;
    emit recordingRootChanged();
}

void DirectScan::record(const cv::Mat& bgr, std::int64_t t_ms) {
    CUBE_ZONE_SCOPED_N("direct_scan.record");
    if (recording_root_.isEmpty() || recording_refused_) {
        return;
    }
    if (!recorder_.running()) {
        // One folder per scan, named for when it began.
        const QString dir = QDir(recording_root_)
                                .filePath(QDateTime::currentDateTime().toString(
                                    QStringLiteral("yyyyMMdd-HHmmss-zzz")));
        cube::vision::recording::Manifest manifest;
        manifest.motion_script = "direct scan";
        // Written only when it is not 3, so a 3x3's recording is the file it always was.
        manifest.size = size_;
        // The reader's own frames only: the full-resolution copy shows more
        // of the room and nothing the reader uses.
        if (const auto problem = recorder_.start(dir.toStdString(), manifest, false);
            problem.has_value()) {
            qWarning("DirectScan: not recording this scan: %s", problem->c_str());
            recording_refused_ = true;
            return;
        }
    }
    recorder_.push(bgr, t_ms);
}

void DirectScan::finishRecording() {
    if (recorder_.running()) {
        // A recording the writer could not finish says so in its stats file,
        // which nobody opens unless they know to: tell the developer who
        // turned recording on.
        if (const auto stats = recorder_.stop(); stats.failed > 0) {
            qWarning("DirectScan: the recording is incomplete, %d frame(s) were not written: %s",
                     stats.failed, stats.error.c_str());
        }
    }
    recording_refused_ = false;
}

QVariantList DirectScan::stickerPoints() const {
    QVariantList points;
    if (read_.has_value()) {
        for (const cv::Point2f& centre : read_->centres) {
            points.append(QPointF(static_cast<double>(centre.x), static_cast<double>(centre.y)));
        }
    }
    return points;
}

std::optional<Reading> DirectScan::reviewReading() const {
    if (!reviewable()) {
        return std::nullopt;
    }
    const cube::vision::ScanDiagnosis diagnosis = scan_.diagnose();
    if (scanned_n_.has_value()) {
        return reading_from_cube(scanned_n_->cube, diagnosis.naming);
    }
    if (diagnosis.naming.has_value()) {
        return reading_from_named_sides(*diagnosis.naming);
    }
    return std::nullopt;
}

QVariantList DirectScan::sides() const {
    QVariantList out;
    for (const auto& side : sides_) {
        QVariantList colors;
        for (const cv::Vec3f& lab : side.lab) {
            colors.append(colour_string(lab));
        }
        out.append(QVariantMap{{"colors", colors}, {"inView", side.in_view}});
    }
    return out;
}

cv::Mat DirectScan::latestFrame() const {
    const std::scoped_lock lock(frame_mutex_);
    return latest_frame_;
}

QVariantMap DirectScan::lock() const {
    using cube::platform::camera::PhotometryControl;
    const auto text = [](std::string_view value) {
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    };

    QVariantMap out;
    out.insert(QStringLiteral("reported"), lock_report_.has_value());
    out.insert(QStringLiteral("supported"), cube::platform::camera::photometry_control_supported());
    if (lock_report_.has_value()) {
        const auto& report = *lock_report_;
        out.insert(
            QStringLiteral("exposure"),
            text(cube::app::records::id_of(report.result(PhotometryControl::exposure).outcome)));
        out.insert(QStringLiteral("whiteBalance"),
                   text(cube::app::records::id_of(
                       report.result(PhotometryControl::auto_white_balance).outcome)));
        out.insert(QStringLiteral("summary"), QString::fromStdString(report.summary()));
    }
    return out;
}

bool DirectScan::diagnosing() const noexcept {
    return diagnostics_ != nullptr && diagnostics_->enabled();
}

void DirectScan::noteCamera(std::string_view event) const {
    if (!diagnosing()) {
        return;
    }
    using cube::diagnostics::Json;
    Json data = Json::object();
    data.set("event", event);
    data.set("device", device_id_.toStdString());
    data.set("running", running_);
    data.set("watching", watching_);
    data.set("awaiting_lock", awaiting_lock_);
    data.set("frame_width", frame_width_);
    data.set("frame_height", frame_height_);
    data.set("photometry_control_supported",
             cube::platform::camera::photometry_control_supported());
    data.set("lock",
             lock_report_.has_value() ? cube::app::records::photometry(*lock_report_) : Json());
    if (!camera_error_.isEmpty()) {
        data.set("error", camera_error_.toStdString());
    }
    diagnostics_->write("camera", std::move(data));
}

cube::diagnostics::Json DirectScan::diagnosticState() const {
    using cube::diagnostics::Json;
    Json state = Json::object();
    state.set("running", running_);
    state.set("watching", watching_);
    state.set("awaiting_lock", awaiting_lock_);
    state.set("complete", scanned_n_.has_value());
    state.set("size", size_);
    state.set("conflicted", conflicted_);
    state.set("ambiguous", ambiguous_);
    state.set("sides_read", sides_.size());
    state.set("frame_width", frame_width_);
    state.set("frame_height", frame_height_);
    state.set("frames_seen", frame_seq_);
    state.set("device", device_id_.toStdString());
    state.set("recording_root", recording_root_.toStdString());
    state.set("lock",
              lock_report_.has_value() ? cube::app::records::photometry(*lock_report_) : Json());
    state.set("scan", cube::diagnostics::to_json(scan_.diagnose()));
    if (scanned_n_.has_value()) {
        state.set("scanned_state", cube::diagnostics::cube_json(scanned_n_->cube));
        state.set("session_colours", cube::diagnostics::to_json(scanned_n_->colours));
    }
    return state;
}

cube::Result<cube::vision::ScannedCubeN> DirectScan::evaluateScan(std::string_view trigger) {
    // One evaluation answers both what the cube is and, when the sides fit
    // together in more than one way, which corner to ask the person for.
    cube::vision::ScanDiagnosis diagnosis = scan_.diagnose();
    settling_corner_ = settling_corner_of(diagnosis);
    if (!diagnosing()) {
        return std::move(diagnosis.result_n);
    }
    std::string digest = digest_of(diagnosis);
    if (digest == last_scan_digest_) {
        ++scans_unchanged_;
        return std::move(diagnosis.result_n);
    }
    cube::diagnostics::Json data = cube::diagnostics::to_json(diagnosis);
    data.set("trigger", trigger);
    // How many evaluations since the last record said the same.
    data.set("unchanged_since_previous", scans_unchanged_);
    diagnostics_->write("scan", std::move(data));
    last_scan_digest_ = std::move(digest);
    scans_unchanged_ = 0;
    return std::move(diagnosis.result_n);
}

void DirectScan::setSize(int size) {
    if (size < 2 || size > 9) {
        return;
    }
    // A size that is asked for is the person's, whatever it was found to be.
    const bool was_auto = auto_size_;
    auto_size_ = false;
    if (was_auto) {
        emit autoSizeChanged();
    }
    if (size == size_) {
        return;
    }
    size_ = size;
    scan_ = cube::vision::SideScan(size);
    restart();
    emit sizeChanged();
}

void DirectScan::adoptSize(int size) {
    size_ = size;
    scan_ = cube::vision::SideScan(size);
    restart();
    emit sizeChanged();
}

void DirectScan::useAutoSize() {
    const bool was_auto = auto_size_;
    auto_size_ = true;
    if (!was_auto) {
        emit autoSizeChanged();
    }
    if (size_ != 3) {
        adoptSize(3);
        return;
    }
    restart();
}

void DirectScan::useDefaultSize() {
    if (default_size_ != 3) {
        setSize(default_size_);
        return;
    }
    if (auto_size_ && size_ == 3 && !size_locked_) {
        return;
    }
    useAutoSize();
}

std::optional<int> DirectScan::detectedSize(const cv::Mat& bgr, bool read_here) {
    // The reader of N x N finds a grid of fewer in a bigger side (a 5x5 reads at 2, 3, 4
    // and 5, a 3x3 at 2 and 3, measured on drawings) and never a bigger grid than the
    // side has, so the cube's size is the largest the frame reads at: up from this
    // size while it reads, or down from it until it does.
    constexpr int kSmallest = 2;
    constexpr int kLargest = 5;
    const auto reads_at = [&bgr](int size) {
        const auto prepared = cube::vision::prepare_frame(bgr, size);
        return prepared.has_value() && cube::vision::read_side(*prepared, size).has_value();
    };
    int found = 0;
    if (read_here) {
        found = size_;
        while (found < kLargest && reads_at(found + 1)) {
            ++found;
        }
    } else {
        for (int size = std::min(size_, kLargest + 1) - 1; size >= kSmallest; --size) {
            if (reads_at(size)) {
                found = size;
                break;
            }
        }
    }
    if (found == 0) {
        return std::nullopt;
    }
    // A hand over a few stickers reads smaller, never bigger, so the size is the largest
    // that most of the last frames read at.
    constexpr std::size_t kFrames = 8;
    constexpr std::size_t kAgreeing = 5;
    size_votes_.push_back(found);
    if (size_votes_.size() > kFrames) {
        size_votes_.erase(size_votes_.begin());
    }
    if (size_votes_.size() < kAgreeing) {
        return std::nullopt;
    }
    for (int size = kLargest; size >= kSmallest; --size) {
        const auto reading = static_cast<std::size_t>(
            std::ranges::count_if(size_votes_, [size](int vote) { return vote >= size; }));
        if (reading >= kAgreeing) {
            return size;
        }
    }
    return std::nullopt;
}

void DirectScan::start(const QString& deviceId) {
    setWatching(false);
    if (start_watcher_.isRunning()) {
        // Back on the scan screen while the camera is still opening: cancel
        // the stop queued in between, or the camera would open and close.
        stop_requested_while_starting_ = false;
        restart();
        return;
    }
    // A scan always begins with a camera freshly opened, so its exposure and
    // white balance are metered and fixed for this scan. One left running by
    // the solve screen is still fixed on the scan before.
    stop();
    restart();
    openCamera(deviceId);
}

void DirectScan::watch(const QString& deviceId) {
    setWatching(true);
    if (start_watcher_.isRunning()) {
        stop_requested_while_starting_ = false;
        return;
    }
    if (!running_) {
        openCamera(deviceId);
    }
}

void DirectScan::setWatching(bool watching) {
    if (watching == watching_) {
        return;
    }
    watching_ = watching;
    if (watching_) {
        // Only a scan is recorded. What the solve screen's camera sees is not.
        finishRecording();
    }
    read_.reset();
    reading_ = false;
    capture_progress_ = 0.0;
    emit runningChanged();
    emit frameUpdated();
}

void DirectScan::openCamera(const QString& deviceId) {
    camera_error_.clear();

    device_id_ = deviceId;
    lock_report_.reset();
    // Opening a camera takes seconds on some drivers; keep the GUI alive.
    const cube::platform::camera::DeviceInfo device{.identifier = deviceId.toStdString(),
                                                    .display_name = ""};
    // On the worker, ahead of the camera: the first conversion of a colour in
    // the scan builds tables that take a quarter of a second in a browser, and
    // it would be the first frame of the scan, on the page's main thread, that
    // paid for them. The worker has the time: opening a camera takes longer.
    start_watcher_.setFuture(QtConcurrent::run([device]() {
        cube::vision::warm_up_colour_conversion();
        return cube::platform::camera::start(device);
    }));
    emit runningChanged();
}

void DirectScan::handleStartFinished() {
    const auto result = start_watcher_.result();
    const bool stop_requested = std::exchange(stop_requested_while_starting_, false);
    if (!result.has_value()) {
        camera_error_ = QString::fromStdString(std::string(cube::to_string(result.error())));
        noteCamera("open_failed");
        emit runningChanged();
        return;
    }
    if (stop_requested) {
        cube::platform::camera::stop();
        emit runningChanged();
        return;
    }

    // Freeze exposure and white balance for the scan: the same sticker has
    // to measure the same on every side, and automatics re-meter for
    // whichever colour fills the frame. A camera that cannot be locked still
    // scans; sides are matched within the session either way.
    awaiting_lock_ = cube::platform::camera::request_photometry_lock(
                         cube::platform::camera::PhotometryLockRequest{})
                         .has_value();
    frames_awaiting_lock_ = 0;

    running_ = true;
    noteCamera(awaiting_lock_ ? "opened_lock_requested" : "opened_lock_refused");
    poll_timer_.start();
    emit runningChanged();
}

void DirectScan::stop() {
    if (start_watcher_.isRunning()) {
        stop_requested_while_starting_ = true;
        return;
    }
    if (!running_) {
        return;
    }
    poll_timer_.stop();
    // Also puts the camera's exposure and white balance back as found.
    cube::platform::camera::stop();
    running_ = false;
    noteCamera("stopped");
    watching_ = false;
    awaiting_lock_ = false;
    finishRecording();
    read_.reset();
    reading_ = false;
    {
        // The next scan must not open on this one's last frame.
        const std::scoped_lock lock(frame_mutex_);
        latest_frame_ = cv::Mat();
    }
    frame_seq_ = 0;
    emit runningChanged();
    emit frameUpdated();
}

void DirectScan::restart() {
    // A new scan is a new recording.
    finishRecording();
    scan_.reset();
    previous_frame_ms_.reset();
    read_.reset();
    reading_ = false;
    capture_progress_ = 0.0;
    sides_.clear();
    conflicted_ = false;
    ambiguous_ = false;
    settling_corner_.clear();
    scanned_.reset();
    scanned_n_.reset();
    size_locked_ = false;
    size_votes_.clear();
    if (diagnosing()) {
        diagnostics_->write("scan", cube::diagnostics::Json::object().set("trigger", "restart"));
    }
    last_scan_digest_.clear();
    scans_unchanged_ = 0;
    emit frameUpdated();
    emit sidesChanged();
}

void DirectScan::poll() {
    auto frame = cube::platform::camera::try_pop_latest_frame();
    if (!frame.has_value()) {
        return;
    }
    CUBE_ZONE_SCOPED_N("direct_scan.poll_frame");
    // The lock reports once, when it has been applied or refused. Either
    // way the camera is as steady as it is going to get.
    auto lock_report = cube::platform::camera::take_photometry_report();
    const bool reported = lock_report.has_value();
    if (reported) {
        lock_report_ = std::move(lock_report);
    }
    if (reported || ++frames_awaiting_lock_ > kLockWaitFrames) {
        if (awaiting_lock_) {
            noteCamera(reported ? "lock_reported" : "lock_timed_out");
        }
        awaiting_lock_ = false;
    }
    processFrame(frame->frame, frame->t_ms);
}

void DirectScan::processFrame(const cv::Mat& bgr, std::int64_t t_ms) {
    CUBE_ZONE_SCOPED_N("direct_scan.process_frame");
    CUBE_FRAME_MARK();
    {
        const std::scoped_lock lock(frame_mutex_);
        latest_frame_ = bgr;
    }
    frame_width_ = bgr.cols;
    frame_height_ = bgr.rows;
    ++frame_seq_;
    // How long since the frame before, for the scan's rule that a side must
    // have held still: a camera that settles at 20 frames a second or less, or
    // a frame the page was late for, moves a side further between frames that
    // are further apart without turning it any faster.
    const float interval_ms =
        previous_frame_ms_.has_value() ? static_cast<float>(t_ms - *previous_frame_ms_) : 0.0F;
    previous_frame_ms_ = t_ms;

    if (watching_) {
        // The solve screen's camera: say what side is in view and leave the
        // scan alone - whether or not there is one. Nothing seen here may
        // start or change a scan.
        read_ = cube::vision::read_side(bgr);
        emit frameUpdated();
        emit frameWatched();
        return;
    }

    if (scanned_n_.has_value()) {
        // Scan finished; keep the preview live until the screen changes.
        emit frameUpdated();
        return;
    }

    // Reduced and outlined once: a side square to the camera and two sides at
    // an angle are read from the same preparation, and the second reader only
    // runs on the frames the first found nothing in.
    std::optional<cube::vision::PreparedFrame> prepared = cube::vision::prepare_frame(bgr, size_);
    read_ = prepared.has_value() ? cube::vision::read_side(*prepared, size_) : std::nullopt;
    if (auto_size_ && !size_locked_) {
        // Until a side is captured the cube's size is looked for in the frames.
        if (const auto found = detectedSize(bgr, read_.has_value());
            found.has_value() && *found != size_) {
            adoptSize(*found);
            prepared = cube::vision::prepare_frame(bgr, size_);
            read_ = prepared.has_value() ? cube::vision::read_side(*prepared, size_) : std::nullopt;
        }
    }
    if (awaiting_lock_) {
        // Show where the cube is, but count nothing yet.
        emit frameUpdated();
        return;
    }
    // Exactly the frames the scan is built from: none before the camera has
    // settled, none after the cube is read.
    record(bgr, t_ms);
    scan_.add(read_, interval_ms);
    bool corner_added = false;
    if (!read_.has_value() && size_ <= cube::vision::kMaxCornerSize) {
        // Not a side square to the camera. It may still be two sides in one
        // view, which is what settles a cube whose flat sides fit two ways.
        const auto corner =
            prepared.has_value() ? cube::vision::read_corner(*prepared, size_) : std::nullopt;
        scan_.add_corner(corner, interval_ms);
        corner_added = corner.has_value();
    }

    std::vector<cube::vision::SideScan::Side> sides = scan_.sides();
    // A side is in: the cube's size is settled.
    size_locked_ = size_locked_ || !sides.empty();
    reading_ = scan_.reading();
    const std::size_t held = scan_.reads_in_view();
    capture_progress_ =
        held < cube::vision::SideScan::kMinReads
            ? static_cast<qreal>(held) / static_cast<qreal>(cube::vision::SideScan::kMinReads)
            : 0.0;
    emit frameUpdated();

    // New sides, or - for a scan that is ambiguous - a new view of two at
    // once, which may be what settles it.
    if (same_sides(sides, sides_) && !(ambiguous_ && corner_added)) {
        return;
    }
    sides_ = std::move(sides);

    auto cube = evaluateScan(corner_added ? "corner_added" : "sides_changed");
    conflicted_ = !cube.has_value() && cube.error() != cube::CubeError::face_capture_incomplete;
    ambiguous_ = !cube.has_value() && cube.error() == cube::CubeError::ambiguous_scan;
    if (cube.has_value()) {
        // Nothing more is read, so nothing is marked on the picture.
        scanned_n_ = *cube;
        if (size_ == 3) {
            if (auto three = cube->cube.to_cube3_as_held(); three.has_value()) {
                scanned_ = cube::vision::ScannedCube{
                    .cube = *three, .colours = cube->colours, .cast = cube->cast};
            }
        }
        read_.reset();
        reading_ = false;
        capture_progress_ = 0.0;
        emit frameUpdated();
    }
    // The sixth side is announced with the rest before the scan is: a
    // listener that leaves the scan screen on scanCompleted would otherwise
    // never show it.
    emit sidesChanged();
    if (scanned_n_.has_value()) {
        finishRecording();
        CUBE_MESSAGE("direct_scan.completed");
        emit scanCompleted();
    }
}

}  // namespace cube::app
