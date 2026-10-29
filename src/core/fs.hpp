#pragma once

#include <filesystem>
#include <string>

namespace core::fsys {

std::string read_file(const std::filesystem::path& path);
bool exists(const std::filesystem::path& path);

// Write through a temp file, then rename. Throws on any failure so callers can
// map it to an environment error instead of reporting a silent success.
void write_atomic(const std::filesystem::path& path, const std::string& content);

// Append one line, creating parent directories. Throws on failure.
void append_line(const std::filesystem::path& path, const std::string& line);

void ensure_dir(const std::filesystem::path& path);

} // namespace core::fsys
