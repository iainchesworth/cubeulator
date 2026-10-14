#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace cube::diagnostics {

// An append-only file of lines, written so that another process - or a person
// with `tail -f` - can read it while this one still has it open: every line is
// flushed as it is written, and the file is opened so that readers are not
// kept out.
//
// Bounded: when a line would take the file past `max_bytes` it is moved to
// `<name>.previous<ext>`, replacing the one already there, and a new file is
// begun. So a channel left on cannot fill a disk, and the last stretch before
// a problem is always in one of the two.
class JsonlFile {
public:
    // 64 MiB: about twenty thousand scan records.
    static constexpr std::uintmax_t kDefaultMaxBytes = 64ULL * 1024ULL * 1024ULL;

    JsonlFile() = default;
    ~JsonlFile() { close(); }
    JsonlFile(const JsonlFile&) = delete;
    JsonlFile& operator=(const JsonlFile&) = delete;

    // Creates the directory if need be and opens `path` for appending. What
    // the file already held - the last run's - is moved aside first, so that
    // each run begins a file of its own and `previous` is always the run
    // before. Returns what went wrong, or nothing.
    [[nodiscard]] std::optional<std::string> open(const std::filesystem::path& path,
                                                  std::uintmax_t max_bytes = kDefaultMaxBytes,
                                                  bool move_existing_aside = true);
    void close();
    [[nodiscard]] bool is_open() const;
    [[nodiscard]] std::filesystem::path path() const;
    // `path` with ".previous" before its extension: diagnostics.jsonl ->
    // diagnostics.previous.jsonl.
    [[nodiscard]] static std::filesystem::path previous_path(const std::filesystem::path& path);

    // Writes `line` and a newline, and flushes. `line` must not contain a
    // newline (Json::dump never does). False if the file is not open or the
    // write failed.
    bool write_line(std::string_view line);

private:
    // Moves the open file aside and starts a new one. Caller holds mutex_.
    void rotate();

    mutable std::mutex mutex_;
    std::filesystem::path path_;
    std::ofstream stream_;
    std::uintmax_t bytes_ = 0;
    std::uintmax_t max_bytes_ = kDefaultMaxBytes;
};

}  // namespace cube::diagnostics
