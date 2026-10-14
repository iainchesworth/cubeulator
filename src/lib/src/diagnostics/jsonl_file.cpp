#include <cube/diagnostics/jsonl_file.hpp>

#include <system_error>

namespace cube::diagnostics {

namespace fs = std::filesystem;

std::optional<std::string> JsonlFile::open(const fs::path& path, std::uintmax_t max_bytes,
                                           bool move_existing_aside) {
    const std::scoped_lock lock(mutex_);
    if (stream_.is_open()) {
        stream_.close();
    }

    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            return "could not create " + path.parent_path().string() + ": " + ec.message();
        }
    }
    bytes_ = 0;
    if (fs::exists(path, ec) && !ec) {
        if (move_existing_aside) {
            // Replaces what is there: the file is a log, and only the run
            // before is kept.
            fs::rename(path, previous_path(path), ec);
            if (ec) {
                fs::remove(path, ec);
            }
        } else {
            bytes_ = fs::file_size(path, ec);
            if (ec) {
                bytes_ = 0;
            }
        }
    }

    stream_.open(path, std::ios::out | std::ios::app | std::ios::binary);
    if (!stream_.is_open()) {
        return "could not open " + path.string();
    }
    path_ = path;
    max_bytes_ = max_bytes;
    return std::nullopt;
}

void JsonlFile::close() {
    const std::scoped_lock lock(mutex_);
    if (stream_.is_open()) {
        stream_.close();
    }
}

bool JsonlFile::is_open() const {
    const std::scoped_lock lock(mutex_);
    return stream_.is_open();
}

fs::path JsonlFile::path() const {
    const std::scoped_lock lock(mutex_);
    return path_;
}

fs::path JsonlFile::previous_path(const fs::path& path) {
    fs::path previous = path;
    previous.replace_extension();
    previous += ".previous";
    previous += path.extension();
    return previous;
}

bool JsonlFile::write_line(std::string_view line) {
    const std::scoped_lock lock(mutex_);
    if (!stream_.is_open()) {
        return false;
    }
    const std::uintmax_t needed = line.size() + 1;
    if (bytes_ > 0 && bytes_ + needed > max_bytes_) {
        rotate();
        if (!stream_.is_open()) {
            return false;
        }
    }
    stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
    stream_.put('\n');
    stream_.flush();
    if (!stream_) {
        return false;
    }
    bytes_ += needed;
    return true;
}

void JsonlFile::rotate() {
    stream_.close();
    std::error_code ec;
    fs::rename(path_, previous_path(path_), ec);
    if (ec) {
        fs::remove(path_, ec);
    }
    stream_.open(path_, std::ios::out | std::ios::app | std::ios::binary);
    bytes_ = 0;
}

}  // namespace cube::diagnostics
