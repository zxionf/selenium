#pragma once

#include <filesystem>
#include <string>

#include "core/sele.hpp"

namespace core::session {

std::string resolve_id(const std::string& override_id);

struct Pointer {
    std::string line;
    std::string task;
};

std::filesystem::path path_of(const sele::Layout& layout, const std::string& session_id);
Pointer read(const sele::Layout& layout, const std::string& session_id);
void write(const sele::Layout& layout, const std::string& session_id, const Pointer& pointer);

} // namespace core::session
