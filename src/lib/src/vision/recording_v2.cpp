#include <cube/vision/recording_v2.hpp>

#include <cube/solver/cube.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace cube::vision::recording {

namespace fs = std::filesystem;

AnalysisFrame analysis_downscale(const cv::Mat& full_bgr) {
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

// Tiny flat-object JSON value reader: given the raw file text, finds
// "key" and returns its string value, std::nullopt for null/absent.
// Tolerant by design (unknown keys ignored, ordering free); NOT a general
// JSON parser - the manifest is a flat object of string-or-null values
// per docs/recording-format.md.
std::optional<std::string> json_string_field(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = text.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    ++pos;
    while (pos < text.size() && (std::isspace(static_cast<unsigned char>(text[pos])) != 0)) {
        ++pos;
    }
    if (pos >= text.size() || text[pos] != '"') {
        return std::nullopt;  // null or malformed - both read as absent
    }
    std::string value;
    for (++pos; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (ch == '\\' && pos + 1 < text.size()) {
            const char next = text[++pos];
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

}  // namespace

std::optional<std::string> state_string_problem(const std::string& state) {
    if (state.size() != 54) {
        return "state must be 54 characters, got " + std::to_string(state.size());
    }
    std::array<std::array<cube::solver::Color, 9>, 6> facelets{};
    for (std::size_t i = 0; i < 54; ++i) {
        const auto face = face_from_letter(state[i]);
        if (!face) {
            return "state[" + std::to_string(i) + "] is '" + std::string(1, state[i]) +
                   "', expected one of URFDLB";
        }
        const auto slot = kManifestFaceOrder[i / 9];
        facelets[static_cast<std::size_t>(slot)][i % 9] = cube::solver::canonical_color(*face);
    }
    // Centres are positionally fixed by the format; a wrong centre letter
    // is caught here with a clearer message than the oracle's.
    static constexpr std::array<char, 6> kCentreLetters{'U', 'R', 'F', 'D', 'L', 'B'};
    for (std::size_t f = 0; f < 6; ++f) {
        const std::size_t centre = f * 9 + 4;
        if (state[centre] != kCentreLetters[f]) {
            return "state[" + std::to_string(centre) + "] must be the centre '" +
                   std::string(1, kCentreLetters[f]) + "', got '" + std::string(1, state[centre]) +
                   "'";
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

std::optional<std::string> write_manifest(const std::string& dir, const Manifest& manifest) {
    if (manifest.state) {
        if (auto problem = state_string_problem(*manifest.state)) {
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
    if (text.find('{') == std::string::npos) {
        return std::nullopt;
    }
    Manifest manifest;
    manifest.state = json_string_field(text, "state");
    manifest.lighting = json_string_field(text, "lighting").value_or("");
    manifest.motion_script = json_string_field(text, "motion_script").value_or("");
    manifest.camera = json_string_field(text, "camera").value_or("");
    manifest.intrinsics = json_string_field(text, "intrinsics");
    manifest.cube = json_string_field(text, "cube").value_or("");
    return manifest;
}

std::optional<std::string> check_recording(const std::string& dir) {
    std::ifstream index_stream(fs::path(dir) / std::string(kIndexCsvName));
    if (!index_stream) {
        return "no " + std::string(kIndexCsvName) + " under " + dir;
    }
    const auto rows = read_index(index_stream);
    if (rows.empty()) {
        return "index has no rows";
    }
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
    if (const auto manifest = read_manifest(dir); manifest && manifest->state) {
        if (auto problem = state_string_problem(*manifest->state)) {
            return "manifest state invalid: " + *problem;
        }
    }
    // Recorder-side drop audit (recording_stats.json, written by stop()).
    // Absent file = pre-v2 or externally-assembled recording: no verdict.
    if (std::ifstream stats_file(fs::path(dir) / "recording_stats.json"); stats_file) {
        std::stringstream buffer;
        buffer << stats_file.rdbuf();
        const std::string text = buffer.str();
        if (const auto pos = text.find("\"dropped\""); pos != std::string::npos) {
            if (const auto colon = text.find(':', pos); colon != std::string::npos) {
                const int dropped = std::atoi(text.c_str() + colon + 1);
                if (dropped > 0) {
                    return "recorder dropped " + std::to_string(dropped) +
                           " frame(s) (queue overflow)";
                }
            }
        }
    }
    return std::nullopt;
}

AsyncRecorder::~AsyncRecorder() {
    if (running_) {
        (void)stop();
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
    running_ = true;
    writer_ = std::thread([this] { writer_loop(); });
    return std::nullopt;
}

void AsyncRecorder::push(const cv::Mat& full_bgr, std::int64_t t_ms) {
    if (!running_) {
        return;
    }
    std::unique_lock lock(mutex_);
    ++stats_.pushed;
    if (queue_.size() >= kQueueCapacity) {
        // Never block the producer thread: drop and COUNT. Passes are
        // numbered writer-side, so a drop leaves no index gap - the count
        // persisted by stop() into recording_stats.json is the honest
        // record, and check_recording fails the clip on any drop. The
        // throughput bench exists to prove this never fires in practice.
        ++stats_.dropped;
        return;
    }
    queue_.push_back(Job{full_bgr.clone(), t_ms});
    stats_.queue_peak = std::max(stats_.queue_peak, static_cast<int>(queue_.size()));
    lock.unlock();
    cv_.notify_one();
}

AsyncRecorder::Stats AsyncRecorder::stop() {
    if (!running_) {
        return stats_;
    }
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_one();
    writer_.join();
    running_ = false;
    // The integrity record check_recording audits: a dropped frame is a
    // failed clip even though the index itself stays contiguous.
    std::ofstream stats_file(fs::path(dir_) / "recording_stats.json", std::ios::trunc);
    stats_file << "{\"pushed\": " << stats_.pushed << ", \"written\": " << stats_.written
               << ", \"dropped\": " << stats_.dropped << ", \"queue_peak\": " << stats_.queue_peak
               << "}\n";
    return stats_;
}

void AsyncRecorder::writer_loop() {
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
        const auto analysis = analysis_downscale(job.full);
        const std::string name = frame_filename(pass_);
        cv::imwrite((dir / name).string(), analysis.frame, {cv::IMWRITE_JPEG_QUALITY, 90});
        if (record_full_res_) {
            cv::imwrite((dir / std::string(kFullResSubdir) / name).string(), job.full,
                        {cv::IMWRITE_JPEG_QUALITY, kFullResJpegQuality});
        }
        index << pass_ << ',' << job.t_ms << ',' << analysis.frame.cols << ','
              << analysis.frame.rows << ',' << job.full.cols << ',' << job.full.rows << '\n';
        index.flush();
        ++pass_;
        {
            std::lock_guard lock(mutex_);
            ++stats_.written;
        }
    }
}

}  // namespace cube::vision::recording
