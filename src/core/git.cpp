#include "core/git.hpp"

#include "core/entity.hpp"
#include "core/event.hpp"
#include "core/fs.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>

namespace core::git {

namespace {

std::string quote(const std::string& value) {
    std::string out = "'";
    for (const char character : value) {
        if (character == '\'') {
            out += "'\\''";
        } else {
            out += character;
        }
    }
    out += "'";
    return out;
}

std::string trim(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

std::vector<std::string> lines(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream stream(value);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) result.push_back(line);
    }
    return result;
}

} // namespace

bool present(const std::filesystem::path& root) {
    return core::fsys::exists(root / ".git");
}

Result run(const std::filesystem::path& root, const std::vector<std::string>& args) {
    std::string command = "git -C " + quote(root.string());
    for (const auto& arg : args) command += " " + quote(arg);
    command += " 2>/dev/null";

    Result result;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        result.code = 1;
        return result;
    }
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }
    const int status = pclose(pipe);
    result.code = status == 0 ? 0 : 1;
    result.output = trim(result.output);
    return result;
}

std::string head(const store::Paths& paths) {
    const auto result = run(paths.root, {"rev-parse", "HEAD"});
    return result.code == 0 ? result.output : "";
}

bool is_ancestor(const store::Paths& paths, const std::string& older,
                 const std::string& newer) {
    if (older.empty() || newer.empty()) return false;
    return run(paths.root, {"merge-base", "--is-ancestor", older, newer}).code == 0;
}

std::string tree(const store::Paths& paths) {
    const auto result = run(paths.root, {"rev-parse", "HEAD^{tree}"});
    return result.code == 0 ? result.output : "";
}

std::string branch(const store::Paths& paths) {
    const auto result = run(paths.root, {"rev-parse", "--abbrev-ref", "HEAD"});
    return result.code == 0 ? result.output : "";
}

std::vector<std::string> files_changed(const store::Paths& paths) {
    const auto result = run(paths.root, {"show", "--name-only", "--pretty=format:"});
    return lines(result.output);
}

std::string diff_stat(const store::Paths& paths) {
    return run(paths.root, {"show", "--stat", "--oneline", "--pretty=format:"}).output;
}

std::string kind_name(DriftKind kind) {
    switch (kind) {
    case DriftKind::D1:
        return "D1";
    case DriftKind::D2:
        return "D2";
    case DriftKind::D3:
        return "D3";
    case DriftKind::D6:
        return "D6";
    case DriftKind::NONE:
        break;
    }
    return "none";
}

Drift detect_drift(const store::Paths& paths) {
    Drift drift;
    if (!present(paths.root)) return drift;

    const std::string now = head(paths);
    if (now.empty()) return drift;

    const auto fingerprint = paths.cache / "git-fingerprint.json";
    std::string previous;
    std::string previous_branch;
    if (core::fsys::exists(fingerprint)) {
        try {
            const auto value = nlohmann::json::parse(core::fsys::read_file(fingerprint));
            previous = value.value("head", "");
            previous_branch = value.value("branch", "");
        } catch (...) {
            previous.clear();
            previous_branch.clear();
        }
    }
    const auto current_branch = branch(paths);
    const auto write_fingerprint = [&]() {
        if (!core::fsys::exists(paths.cache)) return;
        core::fsys::write_atomic(
            fingerprint,
            nlohmann::json({{"head", now}, {"branch", current_branch}}).dump(2) + "\n");
    };
    if (previous.empty()) {
        write_fingerprint();
        return drift;
    }
    if (previous == now) {
        if (!previous_branch.empty() && !current_branch.empty() &&
            previous_branch != current_branch) {
            drift.kind = DriftKind::D6;
            drift.old_head = previous;
            drift.new_head = now;
            write_fingerprint();
        }
        return drift;
    }

    drift.old_head = previous;
    drift.new_head = now;
    if (is_ancestor(paths, previous, now)) {
        drift.kind = DriftKind::D1;
        write_fingerprint();
        if (core::fsys::exists(paths.log)) {
            model::Event event;
            event.ts = model::now_utc();
            event.actor = "sele";
            event.op = "git.drift";
            event.entity = {{"type", "git"}};
            event.before = previous;
            event.after = now;
            event.reason = "D1 HEAD advanced";
            core::event::append(paths, event);
        }
    } else if (is_ancestor(paths, now, previous)) {
        drift.kind = DriftKind::D2;
    } else {
        drift.kind = DriftKind::D3;
    }
    return drift;
}

} // namespace core::git
