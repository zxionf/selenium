#include "core/root.hpp"
#include <filesystem>
#include <stdexcept>

namespace core {

bool is_initialized(const std::filesystem::path& start) {
    std::filesystem::path current = std::filesystem::absolute(start);

    while (true) {
        if (std::filesystem::is_directory(current / ".design") &&
            std::filesystem::is_directory(current / ".agent")) {
            return true;
        }

        auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return false;
}

std::filesystem::path find_design_root(const std::filesystem::path& start) {
    std::filesystem::path current = std::filesystem::absolute(start);

    while (true) {
        std::filesystem::path design_dir = current / ".design";
        if (std::filesystem::exists(design_dir) && std::filesystem::is_directory(design_dir)) {
            return current;
        }

        auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    throw std::runtime_error("No .design directory found. Run from a project root or use --root.");
}

std::filesystem::path find_agent_root(const std::filesystem::path& start) {
    std::filesystem::path current = std::filesystem::absolute(start);

    while (true) {
        std::filesystem::path agent_dir = current / ".agent";
        if (std::filesystem::exists(agent_dir) && std::filesystem::is_directory(agent_dir)) {
            return current;
        }

        auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    throw std::runtime_error("No .agent directory found. Run from a project root or use --root.");
}

} // namespace core