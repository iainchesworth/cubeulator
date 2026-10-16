// The developer diagnostics channel in the app (docs/diagnostics.md): that it
// is off and silent until switched on, that it can be switched on and off
// while the controllers run, what the scan, the camera, the camera's hold
// verdicts and the solve write to it, and that a snapshot captures the lot.
//
// Driven the way the other controller tests are: drawn frames go in through
// DirectScan::processFrame, so no camera is opened.
#include <catch2/catch_test_macros.hpp>

#include "controllers/app_controllers.hpp"
#include "controllers/diagnostic_records.hpp"
#include "controllers/diagnostics.hpp"
#include "drawn_cube.hpp"

#include <cube/diagnostics/json.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/recording_v2.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <filesystem>
#include <string>
#include <vector>

using cube::app::AppControllers;
using cube::app::Diagnostics;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::test::draw_side;
using cube::test::Pose;
using cube::test::room;
using cube::vision::SideScan;

namespace {

constexpr int kFramesToCapture = static_cast<int>(SideScan::kMinReads) + 1;

// A scratch directory the channel writes into.
struct Scratch {
    QTemporaryDir dir;
    [[nodiscard]] std::filesystem::path path() const {
        return std::filesystem::path(dir.path().toStdU16String()) / "diagnostics";
    }
    [[nodiscard]] QString file() const {
        return QString::fromStdU16String(path().u16string()) + "/diagnostics.jsonl";
    }
};

// Every line of the file as a parsed record. A line that is not a JSON
// object fails the test.
[[nodiscard]] std::vector<QJsonObject> records_in(const QString& file) {
    std::vector<QJsonObject> records;
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        return records;
    }
    while (!in.atEnd()) {
        const QByteArray line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        INFO("line: " << line.toStdString());
        REQUIRE(error.error == QJsonParseError::NoError);
        REQUIRE(doc.isObject());
        records.push_back(doc.object());
    }
    return records;
}

[[nodiscard]] std::vector<QJsonObject> of_kind(const std::vector<QJsonObject>& records,
                                               const QString& kind) {
    std::vector<QJsonObject> matching;
    for (const QJsonObject& record : records) {
        if (record.value("kind").toString() == kind) {
            matching.push_back(record);
        }
    }
    return matching;
}

void look_away(AppControllers& controllers) {
    controllers.directScan.processFrame(room({640, 360}));
}

void show_cube(AppControllers& controllers, const Cube& cube) {
    for (const Face face : kAllFaces) {
        for (int i = 0; i < kFramesToCapture + 3; ++i) {
            controllers.directScan.processFrame(
                draw_side(cube::test::outside_view(cube, face), Pose{}));
        }
        look_away(controllers);
    }
}

void wait_for_solution(AppControllers& controllers) {
    QElapsedTimer waited;
    waited.start();
    while (controllers.solve.isSolving() && waited.elapsed() < 60'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    REQUIRE_FALSE(controllers.solve.isSolving());
}

}  // namespace

TEST_CASE("the channel is off until it is switched on, and then nothing is written",
          "[qt][controllers][diagnostics]") {
    const Scratch scratch;
    REQUIRE(scratch.dir.isValid());
    AppControllers controllers(scratch.path());

    CHECK(controllers.diagnostics.available());
    CHECK_FALSE(controllers.diagnostics.enabled());
    CHECK_FALSE(controllers.diagnostics.framesEnabled());
    CHECK(controllers.diagnostics.path().isEmpty());
    CHECK(controllers.diagnostics.framesRoot().isEmpty());

    // A whole scan and its solve with the channel off leave no trace.
    const Cube cube = cube::test::scramble(3);
    show_cube(controllers, cube);
    REQUIRE(controllers.directScan.cube().has_value());
    wait_for_solution(controllers);

    CHECK_FALSE(std::filesystem::exists(scratch.path()));
    // Writing to it, directly, is also nothing.
    controllers.diagnostics.write("scan", cube::diagnostics::Json::object());
    CHECK_FALSE(std::filesystem::exists(scratch.path()));
}

TEST_CASE("the channel is switched on and off while the app runs",
          "[qt][controllers][diagnostics]") {
    const Scratch scratch;
    REQUIRE(scratch.dir.isValid());
    AppControllers controllers(scratch.path());
    int changes = 0;
    QObject::connect(&controllers.diagnostics, &Diagnostics::enabledChanged,
                     [&changes]() { ++changes; });

    controllers.diagnostics.setEnabled(true);
    REQUIRE(controllers.diagnostics.enabled());
    CHECK(changes == 1);
    CHECK(controllers.diagnostics.path().endsWith("diagnostics.jsonl"));
    CHECK(QFile::exists(scratch.file()));

    controllers.diagnostics.write("test", cube::diagnostics::Json::object().set("n", 1));
    auto records = records_in(scratch.file());
    REQUIRE(records.size() == 2);
    CHECK(records.at(0).value("kind").toString() == "session");
    CHECK(records.at(0).value("data").toObject().value("event").toString() == "on");
    CHECK(records.at(0).value("data").toObject().value("pid").toInteger() ==
          QCoreApplication::applicationPid());
    CHECK_FALSE(records.at(0).value("data").toObject().value("version").toString().isEmpty());
    CHECK(records.at(1).value("kind").toString() == "test");
    // A sequence number and a UTC time on every record.
    CHECK(records.at(0).value("seq").toInteger() == 0);
    CHECK(records.at(1).value("seq").toInteger() == 1);
    CHECK(records.at(1).value("time").toString().endsWith("Z"));

    controllers.diagnostics.setEnabled(false);
    CHECK_FALSE(controllers.diagnostics.enabled());
    CHECK(changes == 2);
    CHECK(controllers.diagnostics.path().isEmpty());
    records = records_in(scratch.file());
    REQUIRE(records.size() == 3);
    CHECK(records.at(2).value("data").toObject().value("event").toString() == "off");

    // Off again: nothing more goes into the file.
    controllers.diagnostics.write("test", cube::diagnostics::Json::object());
    CHECK(records_in(scratch.file()).size() == 3);

    // On again in the same run: the same file, appended to.
    controllers.diagnostics.setEnabled(true);
    records = records_in(scratch.file());
    CHECK(records.size() == 4);
    CHECK_FALSE(QFile::exists(scratch.file().replace(".jsonl", ".previous.jsonl")));
}

TEST_CASE("a platform with nowhere to write reports the channel unavailable",
          "[qt][controllers][diagnostics]") {
    Diagnostics diagnostics{std::filesystem::path{}};
    CHECK_FALSE(diagnostics.available());
    diagnostics.setEnabled(true);
    CHECK_FALSE(diagnostics.enabled());
    diagnostics.snapshot();
    CHECK_FALSE(diagnostics.enabled());
}

TEST_CASE("a scan is written as it is evaluated, once for each different outcome",
          "[qt][controllers][diagnostics]") {
    const Scratch scratch;
    REQUIRE(scratch.dir.isValid());
    AppControllers controllers(scratch.path());
    controllers.diagnostics.setEnabled(true);

    const Cube cube = cube::test::scramble(3);
    show_cube(controllers, cube);
    REQUIRE(controllers.directScan.cube().has_value());

    const auto scans = of_kind(records_in(scratch.file()), "scan");
    REQUIRE_FALSE(scans.empty());
    // Many frames, a handful of records: only a change is written.
    CHECK(scans.size() <= 12);

    // The last says the cube was assembled, how, and which cube.
    const QJsonObject last = scans.back().value("data").toObject();
    CHECK(last.value("trigger").toString() == "sides_changed");
    CHECK(last.value("captured").toInt() == 6);
    CHECK(last.value("sides").toArray().size() == 6);
    const QJsonObject first_side = last.value("sides").toArray().at(0).toObject();
    CHECK(first_side.value("lab").toArray().size() == 9);
    CHECK(first_side.value("reads").toInt() >= static_cast<int>(SideScan::kMinReads));
    CHECK(first_side.contains("roll"));

    const QJsonObject naming = last.value("naming").toObject();
    CHECK(naming.value("hue_override").toString() == "applied");
    CHECK(naming.value("clusters").toArray().size() == 6);
    const QJsonObject cluster = naming.value("clusters").toArray().at(0).toObject();
    CHECK(cluster.value("seed").toArray().size() == 3);
    CHECK(cluster.value("mean").toArray().size() == 3);
    CHECK(cluster.value("hue").toObject().contains("hue_deg"));
    CHECK(cluster.value("hue").toObject().contains("chroma"));
    const QJsonObject side_naming = naming.value("sides").toArray().at(0).toObject();
    CHECK(side_naming.value("cluster").toArray().size() == 9);
    CHECK(side_naming.value("name").toArray().size() == 9);
    CHECK(side_naming.value("confidence").toArray().size() == 9);

    CHECK(last.value("assembly").toObject().value("path").toString() == "single_candidate");
    const QJsonObject result = last.value("result").toObject();
    CHECK(result.value("ok").toBool());
    CHECK(result.value("state").toString().toStdString() ==
          cube::vision::recording::state_string(cube));
    CHECK(result.value("colours").toObject().contains("red"));
}

TEST_CASE("a scan that is started over says so", "[qt][controllers][diagnostics]") {
    const Scratch scratch;
    REQUIRE(scratch.dir.isValid());
    AppControllers controllers(scratch.path());
    controllers.diagnostics.setEnabled(true);

    controllers.directScan.restart();
    const auto scans = of_kind(records_in(scratch.file()), "scan");
    REQUIRE(scans.size() == 1);
    CHECK(scans.front().value("data").toObject().value("trigger").toString() == "restart");
}

TEST_CASE("the solve is written when it lands and as it is stepped",
          "[qt][controllers][diagnostics]") {
    const Scratch scratch;
    REQUIRE(scratch.dir.isValid());
    AppControllers controllers(scratch.path());
    controllers.diagnostics.setEnabled(true);

    const Cube cube = cube::test::scramble(3);
    show_cube(controllers, cube);
    wait_for_solution(controllers);
    controllers.solve.nextMove();

    const auto solves = of_kind(records_in(scratch.file()), "solve");
    REQUIRE_FALSE(solves.empty());
    const QJsonObject landed = [&solves] {
        for (const QJsonObject& record : solves) {
            if (record.value("data").toObject().value("event").toString() == "solution") {
                return record.value("data").toObject();
            }
        }
        return QJsonObject{};
    }();
    REQUIRE_FALSE(landed.isEmpty());
    CHECK(landed.value("total_moves").toInt() > 0);
    CHECK_FALSE(landed.value("moves").toString().isEmpty());
    CHECK(landed.value("move_index").toInt() == 0);
    CHECK(landed.value("size").toInt() == 3);
    CHECK(landed.value("cube").toString().toStdString() ==
          cube::vision::recording::state_string(cube));
    CHECK_FALSE(landed.value("current_move").isNull());

    const QJsonObject stepped = solves.back().value("data").toObject();
    CHECK(stepped.value("move_index").toInt() == 1);
    CHECK(stepped.value("cube").toString().toStdString() !=
          cube::vision::recording::state_string(cube));
}

TEST_CASE("the solve of a bigger cube names its size and writes its state at that size",
          "[qt][controllers][diagnostics]") {
    const Scratch scratch;
    REQUIRE(scratch.dir.isValid());
    AppControllers controllers(scratch.path());
    controllers.diagnostics.setEnabled(true);

    const cube::nxn::NCube scrambled =
        cube::nxn::random_scramble({.size = 5, .seed = 4, .moves = 80});
    controllers.solve.solveCube(scrambled);
    wait_for_solution(controllers);
    controllers.solve.nextMove();

    const auto solves = of_kind(records_in(scratch.file()), "solve");
    REQUIRE_FALSE(solves.empty());
    const QJsonObject landed = [&solves] {
        for (const QJsonObject& record : solves) {
            if (record.value("data").toObject().value("event").toString() == "solution") {
                return record.value("data").toObject();
            }
        }
        return QJsonObject{};
    }();
    REQUIRE_FALSE(landed.isEmpty());
    const std::string started = cube::vision::recording::state_string(scrambled);
    CHECK(landed.value("size").toInt() == 5);
    CHECK(started.size() == 150);
    CHECK(landed.value("cube").toString().toStdString() == started);
    CHECK(landed.value("started_from").toString().toStdString() == started);
    CHECK(landed.value("total_moves").toInt() > 0);
    CHECK_FALSE(landed.value("moves").toString().isEmpty());
    CHECK_FALSE(landed.value("current_move").isNull());

    const QJsonObject stepped = solves.back().value("data").toObject();
    CHECK(stepped.value("move_index").toInt() == 1);
    CHECK(stepped.value("size").toInt() == 5);
    CHECK(stepped.value("cube").toString().toStdString() != started);
}

TEST_CASE("a snapshot writes the whole scan and solve in one record, even with the channel off",
          "[qt][controllers][diagnostics]") {
    const Scratch scratch;
    REQUIRE(scratch.dir.isValid());
    AppControllers controllers(scratch.path());

    const Cube cube = cube::test::scramble(3);
    show_cube(controllers, cube);
    wait_for_solution(controllers);
    REQUIRE_FALSE(controllers.diagnostics.enabled());

    controllers.diagnostics.snapshot();

    // The channel is as it was - off - and the file holds the header and the
    // one record, then is closed.
    CHECK_FALSE(controllers.diagnostics.enabled());
    const auto records = records_in(scratch.file());
    REQUIRE(records.size() == 2);
    CHECK(records.at(0).value("kind").toString() == "session");
    CHECK(records.at(0).value("data").toObject().value("event").toString() == "snapshot");
    REQUIRE(records.at(1).value("kind").toString() == "snapshot");

    const QJsonObject data = records.at(1).value("data").toObject();
    const QJsonObject scan = data.value("scan").toObject();
    CHECK(scan.value("complete").toBool());
    CHECK(scan.value("sides_read").toInt() == 6);
    CHECK(scan.value("scanned_state").toString().toStdString() ==
          cube::vision::recording::state_string(cube));
    CHECK(scan.value("session_colours").toObject().contains("orange"));
    CHECK(scan.value("scan").toObject().value("sides").toArray().size() == 6);
    CHECK(scan.value("scan").toObject().value("result").toObject().value("ok").toBool());

    const QJsonObject solve = data.value("solve").toObject();
    CHECK(solve.value("total_moves").toInt() > 0);
    CHECK_FALSE(solve.value("moves").toString().isEmpty());
    CHECK(solve.value("move_index").toInt() == 0);
    CHECK(solve.value("cube").toString().toStdString() ==
          cube::vision::recording::state_string(cube));
    CHECK_FALSE(solve.value("method").toString().isEmpty());

    const QJsonObject held = data.value("held").toObject();
    CHECK(held.value("hold").toObject().value("up").toString() == "U");
    CHECK(held.value("hold").toObject().value("front").toString() == "F");

    // A second one with the channel on lands in the same file, after the rest.
    controllers.diagnostics.setEnabled(true);
    controllers.diagnostics.snapshot();
    CHECK(controllers.diagnostics.enabled());
    CHECK(of_kind(records_in(scratch.file()), "snapshot").size() == 2);
}

TEST_CASE("the held cube's verdicts are written, and CUBE_HELD_LOG is no longer read",
          "[qt][controllers][diagnostics]") {
    const Scratch scratch;
    REQUIRE(scratch.dir.isValid());
    AppControllers controllers(scratch.path());
    controllers.diagnostics.setEnabled(true);

    const Cube cube = cube::test::scramble(3);
    show_cube(controllers, cube);
    wait_for_solution(controllers);
    controllers.directScan.setWatching(true);

    // The cube held as the screen asks, long enough to be trusted.
    const cv::Mat frame = draw_side(
        cube::solver::camera_view(controllers.solve.currentCube(), cube::solver::kStatedHold),
        Pose{});
    for (int i = 0; i < cube::app::HeldCube::kFramesToTrust + 2; ++i) {
        controllers.directScan.processFrame(frame);
    }

    const auto held = of_kind(records_in(scratch.file()), "held");
    REQUIRE_FALSE(held.empty());
    const QJsonObject verdict = held.front().value("data").toObject();
    CHECK(verdict.value("verdict").toString() == "held");
    CHECK(verdict.value("move_index").toInt() == 0);
    CHECK_FALSE(verdict.value("asked").isNull());
    CHECK(verdict.value("stickers_found").toInt() == 9);
    CHECK(verdict.value("found").toArray().size() == 9);
    CHECK(verdict.value("lab").toArray().size() == 9);
    CHECK(verdict.value("as_it_stood").toObject().contains("distance"));
    CHECK(verdict.value("as_it_stood").toObject().contains("stickers_off"));
    CHECK(verdict.value("after_move").toObject().contains("distance"));
    // One record for the change, not one for every frame.
    CHECK(held.size() <= 3);
}

TEST_CASE("the photometry lock is described as the driver accepted it",
          "[qt][controllers][diagnostics]") {
    using cube::platform::camera::ControlOutcome;
    using cube::platform::camera::PhotometryControl;
    using cube::platform::camera::PhotometryLockReport;

    PhotometryLockReport report;
    report.platform_supported = true;
    report.white_balance_kelvin = 4700.0;
    const auto set = [&report](PhotometryControl control, ControlOutcome outcome, double requested,
                               double readback) {
        auto& entry = report.controls.at(static_cast<std::size_t>(control));
        entry.control = control;
        entry.outcome = outcome;
        entry.requested = requested;
        entry.readback = readback;
    };
    set(PhotometryControl::auto_white_balance, ControlOutcome::applied, 0.0, 0.0);
    set(PhotometryControl::exposure, ControlOutcome::applied, -6.0, -6.0);
    set(PhotometryControl::brightness, ControlOutcome::rejected, 90.0, 128.0);
    set(PhotometryControl::saturation, ControlOutcome::unsupported, 0.0, 0.0);

    const std::string line = cube::app::records::photometry(report).dump();
    const QJsonObject json = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
    REQUIRE_FALSE(json.isEmpty());
    CHECK(json.value("platform_supported").toBool());
    CHECK(json.value("exposure_locked").toBool());
    CHECK_FALSE(json.value("fully_applied").toBool());
    CHECK(json.value("white_balance_kelvin").toDouble() == 4700.0);
    CHECK(json.value("auto_white_balance").toObject().value("outcome").toString() == "applied");
    CHECK(json.value("exposure").toObject().value("readback").toDouble() == -6.0);
    CHECK(json.value("brightness").toObject().value("outcome").toString() == "rejected");
    CHECK(json.value("brightness").toObject().value("requested").toDouble() == 90.0);
    CHECK(json.value("brightness").toObject().value("readback").toDouble() == 128.0);
    CHECK(json.value("saturation").toObject().value("outcome").toString() == "unsupported");
    CHECK_FALSE(json.value("summary").toString().isEmpty());

    PhotometryLockReport without;
    const QJsonObject empty =
        QJsonDocument::fromJson(
            QByteArray::fromStdString(cube::app::records::photometry(without).dump()))
            .object();
    CHECK_FALSE(empty.value("platform_supported").toBool());
    CHECK(empty.value("white_balance_kelvin").isNull());
}

TEST_CASE("camera frames are kept only on their own switch, in a folder beside the file",
          "[qt][controllers][diagnostics]") {
    const Scratch scratch;
    REQUIRE(scratch.dir.isValid());
    AppControllers controllers(scratch.path());

    // Diagnostics on, frames not asked for: no recording.
    controllers.diagnostics.setEnabled(true);
    CHECK(controllers.diagnostics.framesRoot().isEmpty());
    CHECK(controllers.directScan.recordingRoot().isEmpty());

    // Frames asked for: the existing recorder's root is the folder beside the
    // file, and the scan screen's recording marker has something to show.
    controllers.diagnostics.setFramesEnabled(true);
    const QString root = controllers.diagnostics.framesRoot();
    CHECK(root.endsWith("frames"));
    CHECK(controllers.directScan.recordingRoot() == root);

    // Asking for frames first, diagnostics afterwards, is the same thing.
    controllers.diagnostics.setEnabled(false);
    CHECK(controllers.directScan.recordingRoot().isEmpty());
    controllers.diagnostics.setEnabled(true);
    CHECK(controllers.directScan.recordingRoot() == root);

    controllers.diagnostics.setFramesEnabled(false);
    CHECK(controllers.directScan.recordingRoot().isEmpty());
}
