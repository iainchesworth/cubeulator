#include <cube/vision/recording_v2.hpp>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/validate.hpp>
#include <cube/profiling.hpp>
#include <cube/solver/cube.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace cube::vision::recording {

namespace fs = std::filesystem;

AnalysisFrame analysis_downscale(const cv::Mat& full_bgr) {
    CUBE_ZONE_SCOPED_N("vision.analysis_downscale");
    AnalysisFrame out;
    out.frame = full_bgr;
    if (full_bgr.cols > kAnalysisWidth) {
        const double scale = static_cast<double>(kAnalysisWidth) / full_bgr.cols;
        cv::resize(full_bgr, out.frame, cv::Size(), scale, scale, cv::INTER_AREA);
        out.scale_back = static_cast<float>(full_bgr.cols) / static_cast<float>(out.frame.cols);
    }
    return out;
}

namespace {

// State string faces in manifest order (URFDLB, offsets 0,9,...,45) mapped
// to the solver enum, whose storage order is U,D,L,R,F,B.
constexpr std::array<cube::solver::Face, 6> kManifestFaceOrder{
    cube::solver::Face::U, cube::solver::Face::R, cube::solver::Face::F,
    cube::solver::Face::D, cube::solver::Face::L, cube::solver::Face::B,
};

std::optional<cube::solver::Face> face_from_letter(char letter) {
    switch (letter) {
        case 'U':
            return cube::solver::Face::U;
        case 'R':
            return cube::solver::Face::R;
        case 'F':
            return cube::solver::Face::F;
        case 'D':
            return cube::solver::Face::D;
        case 'L':
            return cube::solver::Face::L;
        case 'B':
            return cube::solver::Face::B;
        default:
            return std::nullopt;
    }
}

// Minimal JSON string escaping for manifest values (they are short
// free-text descriptors; control characters have no business here but must
// not corrupt the file if they appear).
void append_json_string(std::string& out, const std::string& text) {
    out.push_back('"');
    for (const char ch : text) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) >= 0x20) {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
}

// The manifest's string fields, by name: a type of its own so that the text
// and the key cannot be passed the wrong way round.
enum class ManifestField : std::uint8_t {
    state,
    size,
    lighting,
    motion_script,
    camera,
    intrinsics,
    cube,
};

// What the recorder keeps in recording_stats.json that check_recording reads.
enum class StatsField : std::uint8_t {
    dropped,
    failed,
    error,
};

constexpr std::string_view field_name(StatsField field) noexcept {
    switch (field) {
        case StatsField::dropped:
            return "dropped";
        case StatsField::failed:
            return "failed";
        case StatsField::error:
            return "error";
    }
    return {};
}

constexpr std::string_view field_name(ManifestField field) noexcept {
    switch (field) {
        case ManifestField::state:
            return "state";
        case ManifestField::size:
            return "size";
        case ManifestField::lighting:
            return "lighting";
        case ManifestField::motion_script:
            return "motion_script";
        case ManifestField::camera:
            return "camera";
        case ManifestField::intrinsics:
            return "intrinsics";
        case ManifestField::cube:
            return "cube";
    }
    return {};
}

// Tiny flat-object JSON value reader: given the raw file text, finds "key"
// and returns where its value starts (past the colon and any spaces),
// std::nullopt when the key is absent. Tolerant by design (unknown keys
// ignored, ordering free); NOT a general JSON parser - the manifest and the
// recorder's stats are flat objects per docs/recording-format.md.
template <typename Field>
std::optional<std::size_t> json_value_start(const std::string& text, Field field) {
    const std::string needle = "\"" + std::string(field_name(field)) + "\"";
    std::size_t pos = text.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    ++pos;
    while (pos < text.size() && (std::isspace(static_cast<unsigned char>(text.at(pos))) != 0)) {
        ++pos;
    }
    return pos;
}

// The key's string value, std::nullopt for null/absent.
template <typename Field>
std::optional<std::string> json_string_field(const std::string& text, Field field) {
    const auto start = json_value_start(text, field);
    if (!start || *start >= text.size() || text.at(*start) != '"') {
        return std::nullopt;  // null or malformed - both read as absent
    }
    std::size_t pos = *start;
    std::string value;
    for (++pos; pos < text.size(); ++pos) {
        const char ch = text.at(pos);
        if (ch == '\\' && pos + 1 < text.size()) {
            const char next = text.at(++pos);
            switch (next) {
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    value.push_back(next);
            }
        } else if (ch == '"') {
            return value;
        } else {
            value.push_back(ch);
        }
    }
    return std::nullopt;  // unterminated string
}

// The key's integer value, std::nullopt for an absent key; zero for a key that
// is not a number. A stream reads it without atoi's silent zero for text and
// without pointer arithmetic.
template <typename Field>
std::optional<int> json_int_field(const std::string& text, Field field) {
    const auto start = json_value_start(text, field);
    if (!start) {
        return std::nullopt;
    }
    int value = 0;
    std::istringstream{text.substr(*start)} >> value;
    return value;
}

}  // namespace

std::string state_string(const cube::nxn::NCube& cube) {
    constexpr std::array<char, 6> kLetters{'U', 'R', 'F', 'D', 'L', 'B'};
    const int size = cube.size();
    const auto n = static_cast<std::size_t>(size);
    std::string out;
    out.reserve(6 * n * n);
    for (const cube::solver::Face face : kManifestFaceOrder) {
        for (int row = 0; row < size; ++row) {
            for (int col = 0; col < size; ++col) {
                // A sticker shows the colour that is some face's centre; that
                // face's letter is the character.
                const cube::solver::Color colour = cube.at(face, row, col);
                char letter = '?';
                for (std::size_t f = 0; f < kManifestFaceOrder.size(); ++f) {
                    if (cube::solver::canonical_color(kManifestFaceOrder.at(f)) == colour) {
                        letter = kLetters.at(f);
                    }
                }
                out.push_back(letter);
            }
        }
    }
    return out;
}

std::string state_string(const cube::solver::Cube& cube) {
    return state_string(cube::nxn::NCube::from_cube3(cube));
}

namespace {

// The cube sizes a manifest may name.
bool is_cube_size(int size) noexcept {
    return size >= cube::nxn::kMinSize && size <= cube::nxn::kMaxSize;
}

std::optional<std::string> size_problem(int size) {
    if (is_cube_size(size)) {
        return std::nullopt;
    }
    return "size must be between " + std::to_string(cube::nxn::kMinSize) + " and " +
           std::to_string(cube::nxn::kMaxSize) + ", got " + std::to_string(size);
}

// state_string_problem for a 3x3: the 54 letters through the 3x3 solver's own
// oracle.
std::optional<std::string> state_string_problem_3x3(const std::string& state) {
    if (state.size() != 54) {
        return "state must be 54 characters, got " + std::to_string(state.size());
    }
    std::array<std::array<cube::solver::Color, 9>, 6> facelets{};
    for (std::size_t i = 0; i < 54; ++i) {
        const auto face = face_from_letter(state.at(i));
        if (!face) {
            return "state[" + std::to_string(i) + "] is '" + std::string(1, state.at(i)) +
                   "', expected one of URFDLB";
        }
        const auto slot = kManifestFaceOrder.at(i / 9);
        facelets.at(static_cast<std::size_t>(slot)).at(i % 9) =
            cube::solver::canonical_color(*face);
    }
    // Centres are positionally fixed by the format; a wrong centre letter
    // is caught here with a clearer message than the oracle's.
    static constexpr std::array<char, 6> kCentreLetters{'U', 'R', 'F', 'D', 'L', 'B'};
    for (std::size_t f = 0; f < 6; ++f) {
        const std::size_t centre = (f * 9) + 4;
        if (state.at(centre) != kCentreLetters.at(f)) {
            return "state[" + std::to_string(centre) + "] must be the centre '" +
                   std::string(1, kCentreLetters.at(f)) + "', got '" +
                   std::string(1, state.at(centre)) + "'";
        }
    }
    const auto cube = cube::solver::Cube::from_facelets(facelets);
    if (!cube.has_value()) {
        return "state does not describe a physically valid cube";
    }
    if (!cube->is_solvable()) {
        return "state describes an unsolvable cube (twist/flip/permutation parity)";
    }
    return std::nullopt;
}

// state_string_problem for any other size: the letters through the N x N
// oracle (cube::nxn::validate). An odd size has a middle sticker on every
// side, fixed by position as the 3x3's centres are; an even size has none, so
// its letters are only colours.
std::optional<std::string> state_string_problem_nxn(const std::string& state, int size) {
    const auto n = static_cast<std::size_t>(size);
    const std::size_t per_face = n * n;
    if (state.size() != 6 * per_face) {
        return "state must be " + std::to_string(6 * per_face) + " characters for a cube of size " +
               std::to_string(size) + ", got " + std::to_string(state.size());
    }
    std::vector<cube::solver::Color> colours(state.size());
    for (std::size_t i = 0; i < state.size(); ++i) {
        const auto face = face_from_letter(state.at(i));
        if (!face) {
            return "state[" + std::to_string(i) + "] is '" + std::string(1, state.at(i)) +
                   "', expected one of URFDLB";
        }
        // The state string's sides come in URFDLB order; an NCube numbers them
        // in the solver's (U, D, L, R, F, B).
        const auto slot = static_cast<std::size_t>(kManifestFaceOrder.at(i / per_face));
        colours.at((slot * per_face) + (i % per_face)) = cube::solver::canonical_color(*face);
    }
    static constexpr std::array<char, 6> kCentreLetters{'U', 'R', 'F', 'D', 'L', 'B'};
    if (size % 2 == 1) {
        for (std::size_t f = 0; f < 6; ++f) {
            const std::size_t middle = (f * per_face) + ((n / 2) * n) + (n / 2);
            if (state.at(middle) != kCentreLetters.at(f)) {
                return "state[" + std::to_string(middle) + "] must be the centre '" +
                       std::string(1, kCentreLetters.at(f)) + "', got '" +
                       std::string(1, state.at(middle)) + "'";
            }
        }
    }
    const auto cube = cube::nxn::NCube::from_facelets(size, colours);
    if (!cube.has_value()) {
        return "state does not describe a physically valid cube";
    }
    if (const auto verdict = cube::nxn::validate(*cube); !verdict.ok()) {
        return "state describes a cube no one could hold: " +
               std::string(cube::nxn::to_string(verdict.fault));
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::string> state_string_problem(const std::string& state, int size) {
    if (auto problem = size_problem(size)) {
        return problem;
    }
    return size == 3 ? state_string_problem_3x3(state) : state_string_problem_nxn(state, size);
}

std::optional<std::string> write_manifest(const std::string& dir, const Manifest& manifest) {
    if (auto problem = size_problem(manifest.size)) {
        return problem;
    }
    if (manifest.state) {
        if (auto problem = state_string_problem(*manifest.state, manifest.size)) {
            return problem;
        }
    }
    std::string out = "{\n";
    const auto field = [&out](const char* key, const std::optional<std::string>& value,
                              bool last = false) {
        out += "  \"";
        out += key;
        out += "\": ";
        if (value) {
            append_json_string(out, *value);
        } else {
            out += "null";
        }
        out += last ? "\n" : ",\n";
    };
    field("state", manifest.state);
    if (manifest.size != 3) {
        // Absent means a 3x3, so a 3x3's file is the one it always was.
        out += "  \"size\": " + std::to_string(manifest.size) + ",\n";
    }
    field("lighting", manifest.lighting);
    field("motion_script", manifest.motion_script);
    field("camera", manifest.camera);
    field("intrinsics", manifest.intrinsics);
    field("cube", manifest.cube, true);
    out += "}\n";

    std::ofstream file(fs::path(dir) / kManifestName, std::ios::trunc);
    if (!file) {
        return "cannot write " + (fs::path(dir) / kManifestName).string();
    }
    file << out;
    return std::nullopt;
}

std::optional<Manifest> read_manifest(const std::string& dir) {
    std::ifstream file(fs::path(dir) / kManifestName);
    if (!file) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    if (!text.contains('{')) {
        return std::nullopt;
    }
    Manifest manifest;
    manifest.state = json_string_field(text, ManifestField::state);
    manifest.size = json_int_field(text, ManifestField::size).value_or(3);
    manifest.lighting = json_string_field(text, ManifestField::lighting).value_or("");
    manifest.motion_script = json_string_field(text, ManifestField::motion_script).value_or("");
    manifest.camera = json_string_field(text, ManifestField::camera).value_or("");
    manifest.intrinsics = json_string_field(text, ManifestField::intrinsics);
    manifest.cube = json_string_field(text, ManifestField::cube).value_or("");
    return manifest;
}

namespace {

// The index rows are one pass each, in order, with time not going backwards,
// and every frame that was not overwritten is on disk.
std::optional<std::string> index_problem(const std::string& dir,
                                         const std::vector<RecordedFrameInfo>& rows) {
    int max_pass = 0;
    for (const auto& row : rows) {
        max_pass = std::max(max_pass, row.pass);
    }
    const bool has_full_res = fs::exists(fs::path(dir) / std::string(kFullResSubdir));
    int expected_pass = rows.front().pass;
    std::int64_t previous_t = rows.front().t_ms;
    for (const auto& row : rows) {
        if (row.pass != expected_pass) {
            return "pass gap: expected " + std::to_string(expected_pass) + ", found " +
                   std::to_string(row.pass) + " (dropped frames)";
        }
        ++expected_pass;
        if (row.t_ms < previous_t) {
            return "t_ms not monotonic at pass " + std::to_string(row.pass);
        }
        previous_t = row.t_ms;
        if (frame_overwritten(row.pass, max_pass)) {
            continue;
        }
        const std::string name = frame_filename(row.pass);
        if (!fs::exists(fs::path(dir) / name)) {
            return "missing analysis frame " + name;
        }
        if (has_full_res && !fs::exists(fs::path(dir) / std::string(kFullResSubdir) / name)) {
            return "missing full-res frame " + std::string(kFullResSubdir) + "/" + name;
        }
    }
    return std::nullopt;
}

// What the recorder said about itself (recording_stats.json, written by
// stop()): frames it dropped and frames it could not write.
struct RecorderStats {
    int dropped = 0;
    int failed = 0;
    std::string error;
};

// Absent file = pre-v2 or externally-assembled recording: no verdict.
std::optional<RecorderStats> read_recorder_stats(const std::string& dir) {
    std::ifstream stats_file(fs::path(dir) / "recording_stats.json");
    if (!stats_file) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << stats_file.rdbuf();
    const std::string text = buffer.str();
    return RecorderStats{
        .dropped = json_int_field(text, StatsField::dropped).value_or(0),
        .failed = json_int_field(text, StatsField::failed).value_or(0),
        .error = json_string_field(text, StatsField::error).value_or(""),
    };
}

}  // namespace

std::optional<std::string> check_recording(const std::string& dir) {
    const auto stats = read_recorder_stats(dir);
    // First, because it is the cause of what follows: a writer that gave up
    // leaves an index with no rows, or short of frames, and those say less.
    if (stats && stats->failed > 0) {
        return "recorder failed to write " + std::to_string(stats->failed) +
               " frame(s): " + (stats->error.empty() ? "no reason recorded" : stats->error);
    }
    std::ifstream index_stream(fs::path(dir) / std::string(kIndexCsvName));
    if (!index_stream) {
        return "no " + std::string(kIndexCsvName) + " under " + dir;
    }
    const auto rows = read_index(index_stream);
    if (rows.empty()) {
        return "index has no rows";
    }
    if (auto problem = index_problem(dir, rows)) {
        return problem;
    }
    if (const auto manifest = read_manifest(dir); manifest) {
        if (auto problem = size_problem(manifest->size)) {
            return "manifest size invalid: " + *problem;
        }
        if (manifest->state) {
            if (auto problem = state_string_problem(*manifest->state, manifest->size)) {
                return "manifest state invalid: " + *problem;
            }
        }
    }
    if (stats && stats->dropped > 0) {
        return "recorder dropped " + std::to_string(stats->dropped) + " frame(s) (queue overflow)";
    }
    return std::nullopt;
}

namespace {

constexpr int kAnalysisJpegQuality = 90;

// The default encoder: returns false for a path it cannot write, and throws a
// cv::Exception for an extension it has no encoder for.
bool imwrite_jpeg(const std::string& path, const cv::Mat& frame, int jpeg_quality) {
    return cv::imwrite(path, frame, {cv::IMWRITE_JPEG_QUALITY, jpeg_quality});
}

// An exception's message without the trailing newline OpenCV's carry.
std::string message_of(const std::exception& error) {
    std::string text = error.what();
    while (!text.empty() && (std::isspace(static_cast<unsigned char>(text.back())) != 0)) {
        text.pop_back();
    }
    return text;
}

}  // namespace

AsyncRecorder::AsyncRecorder(FrameEncoder encoder)
    : encoder_(encoder ? std::move(encoder) : FrameEncoder(imwrite_jpeg)) {}

AsyncRecorder::~AsyncRecorder() {
    if (!running_) {
        return;
    }
    // A destructor must not throw (joining the writer can, in principle), and
    // there is no caller to tell: say so on stderr and carry on.
    try {
        (void)stop();
    } catch (...) {
        std::fputs("AsyncRecorder: stopping the writer failed during destruction\n", stderr);
    }
}

std::optional<std::string> AsyncRecorder::start(const std::string& dir, const Manifest& manifest,
                                                bool record_full_res) {
    if (running_) {
        return "recorder already running";
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (record_full_res) {
        fs::create_directories(fs::path(dir) / std::string(kFullResSubdir), ec);
    }
    if (auto problem = write_manifest(dir, manifest)) {
        return problem;
    }
    {
        std::ofstream header(fs::path(dir) / std::string(kIndexCsvName), std::ios::trunc);
        if (!header) {
            return "cannot write index under " + dir;
        }
        header << kIndexCsvHeader << '\n';
    }
    dir_ = dir;
    record_full_res_ = record_full_res;
    pass_ = 0;
    stats_ = {};
    stopping_ = false;
    writer_failed_ = false;
    running_ = true;
    writer_ = std::thread([this] { writer_loop(); });
    return std::nullopt;
}

void AsyncRecorder::push(const cv::Mat& full_bgr, std::int64_t t_ms) {
    CUBE_ZONE_SCOPED_N("vision.recorder.push");
    if (!running_) {
        return;
    }
    std::unique_lock lock(mutex_);
    ++stats_.pushed;
    if (writer_failed_) {
        // Nothing is writing any more: queueing would only fill the queue and
        // hold the frames in memory, then count them as dropped.
        ++stats_.failed;
        return;
    }
    if (queue_.size() >= kQueueCapacity) {
        // Never block the producer thread: drop and COUNT. Passes are
        // numbered writer-side, so a drop leaves no index gap - the count
        // persisted by stop() into recording_stats.json is the honest
        // record, and check_recording fails the clip on any drop. The
        // throughput bench exists to prove this never fires in practice.
        ++stats_.dropped;
        return;
    }
    queue_.push_back(Job{.full = full_bgr.clone(), .t_ms = t_ms});
    stats_.queue_peak = std::max(stats_.queue_peak, static_cast<int>(queue_.size()));
    lock.unlock();
    cv_.notify_one();
}

AsyncRecorder::Stats AsyncRecorder::stop() {
    if (!running_) {
        return stats_;
    }
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_one();
    writer_.join();
    running_ = false;
    // The integrity record check_recording audits: a dropped frame is a
    // failed clip even though the index itself stays contiguous, and so is a
    // frame that could not be written. A recording that wrote every frame
    // gets the same file it always did: `failed` and `error` appear only when
    // something failed.
    std::ofstream stats_file(fs::path(dir_) / "recording_stats.json", std::ios::trunc);
    stats_file << "{\"pushed\": " << stats_.pushed << ", \"written\": " << stats_.written
               << ", \"dropped\": " << stats_.dropped << ", \"queue_peak\": " << stats_.queue_peak;
    if (stats_.failed > 0) {
        std::string error;
        append_json_string(error, stats_.error);
        stats_file << ", \"failed\": " << stats_.failed << ", \"error\": " << error;
    }
    stats_file << "}\n";
    return stats_;
}

void AsyncRecorder::writer_loop() {
    CUBE_THREAD_NAME("recorder-writer");
    const fs::path dir(dir_);
    std::ofstream index(dir / std::string(kIndexCsvName), std::ios::app);
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || stopping_; });
            if (queue_.empty()) {
                return;  // stopping and drained
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        CUBE_ZONE_SCOPED_N("vision.recorder.write_job");
        if (const auto problem = write_job(job, index)) {
            // Stop here, not on to the next frame: a directory that cannot be
            // written now is not likely to be in 33 ms, and every attempt is
            // a frame's encode spent on a failure already known.
            fail_writer(*problem);
            return;
        }
        ++pass_;
        {
            std::scoped_lock lock(mutex_);
            ++stats_.written;
        }
    }
}

std::optional<std::string> AsyncRecorder::write_job(const Job& job, std::ostream& index) const {
    // Everything that can go wrong here is caught: cv::imwrite returns false
    // for a path it cannot write but throws for an extension with no encoder,
    // resize can throw, and an exception that left the writer thread's
    // function would be std::terminate. `target` is what was being written,
    // for the message.
    std::string target = dir_;
    try {
        const fs::path dir(dir_);
        const auto analysis = analysis_downscale(job.full);
        const std::string name = frame_filename(pass_);
        target = (dir / name).string();
        if (!encoder_(target, analysis.frame, kAnalysisJpegQuality)) {
            return "cannot write " + target;
        }
        if (record_full_res_) {
            target = (dir / std::string(kFullResSubdir) / name).string();
            if (!encoder_(target, job.full, kFullResJpegQuality)) {
                return "cannot write " + target;
            }
        }
        // The row comes last: it claims the frames above are on disk.
        target = (dir / std::string(kIndexCsvName)).string();
        index << pass_ << ',' << job.t_ms << ',' << analysis.frame.cols << ','
              << analysis.frame.rows << ',' << job.full.cols << ',' << job.full.rows << '\n';
        index.flush();
        if (!index) {
            return "cannot append to " + target;
        }
        return std::nullopt;
    } catch (const std::exception& error) {
        return "cannot write " + target + ": " + message_of(error);
    } catch (...) {
        return "cannot write " + target + ": unknown exception";
    }
}

void AsyncRecorder::fail_writer(const std::string& problem) {
    std::deque<Job> abandoned;
    {
        std::scoped_lock lock(mutex_);
        writer_failed_ = true;
        stats_.error = problem;
        // The job in hand, and every one that was waiting behind it.
        stats_.failed += 1 + static_cast<int>(queue_.size());
        abandoned.swap(queue_);
    }
    // `abandoned` frees its frames here, outside the lock push() takes.
}

}  // namespace cube::vision::recording
