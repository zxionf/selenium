#include "core/fs.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace core::fsys {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

void write_atomic(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot open " + temp.string() + " for writing");
        out << content;
        out.flush();
        if (!out) throw std::runtime_error("Failed to write " + temp.string());
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error("Cannot replace " + path.string() + ": " + ec.message());
    }
}

void append_line(const std::filesystem::path& path, const std::string& line) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) throw std::runtime_error("Cannot open " + path.string() + " for appending");
    out << line << '\n';
    out.flush();
    if (!out) throw std::runtime_error("Failed to append to " + path.string());
}

void ensure_dir(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec && !std::filesystem::is_directory(path)) {
        throw std::runtime_error("Cannot create directory " + path.string() + ": " + ec.message());
    }
}

} // namespace core::fsys
