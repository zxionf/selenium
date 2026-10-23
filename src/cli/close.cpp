#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "core/root.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace fs = std::filesystem;

namespace cli {

namespace {

constexpr const char* kActiveChange = "Active Change:";

struct Section {
    std::string title;
    std::string body;
};

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Write through a temp file so an interrupted run cannot leave a truncated
// SPEC.md behind, and fail loudly instead of reporting a silent success.
void write_file(const fs::path& path, const std::string& content) {
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot open " + temp.string() + " for writing");
        out << content;
        out.flush();
        if (!out) throw std::runtime_error("Failed to write " + temp.string());
    }
    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        throw std::runtime_error("Cannot replace " + path.string() + ": " + ec.message());
    }
}

std::string trim(const std::string& value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
    return value.substr(begin, end - begin);
}

// Trim trailing whitespace only: leading indentation is meaningful in nested
// lists and fenced code blocks.
std::string right_trim(const std::string& value) {
    size_t end = value.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
    return value.substr(0, end);
}

// Right-trim every line and drop leading/trailing blank lines.
std::string trim_block(const std::string& text) {
    std::istringstream iss(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(iss, line)) {
        lines.push_back(right_trim(line));
    }
    size_t begin = 0;
    size_t end = lines.size();
    while (begin < end && lines[begin].empty()) ++begin;
    while (end > begin && lines[end - 1].empty()) --end;

    std::string out;
    for (size_t i = begin; i < end; ++i) {
        out += lines[i];
        if (i + 1 < end) out += '\n';
    }
    return out;
}

// A section counts as content when anything is left after stripping HTML
// comments and whitespace. "<!-- note --> real text" is content, "<!-- x -->"
// alone is a template placeholder.
bool has_content(const std::string& body) {
    size_t i = 0;
    while (i < body.size()) {
        if (body.compare(i, 4, "<!--") == 0) {
            size_t end = body.find("-->", i + 4);
            if (end == std::string::npos) return false;
            i = end + 3;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(body[i])) != 0) {
            ++i;
            continue;
        }
        return true;
    }
    return false;
}

// CommonMark fenced code block opening: up to three spaces, then at least
// three backticks or tildes.
bool fence_open(const std::string& line, std::string* marker) {
    size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') ++i;
    if (i >= line.size()) return false;
    char fence = line[i];
    if (fence != '`' && fence != '~') return false;
    size_t run_begin = i;
    while (i < line.size() && line[i] == fence) ++i;
    if (i - run_begin < 3) return false;
    *marker = line.substr(run_begin, i - run_begin);
    return true;
}

bool fence_close(const std::string& line, const std::string& marker) {
    if (marker.empty()) return false;
    size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') ++i;
    if (i >= line.size() || line[i] != marker.front()) return false;
    size_t run_begin = i;
    while (i < line.size() && line[i] == marker.front()) ++i;
    if (i - run_begin < marker.size()) return false;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) != 0) ++i;
    return i == line.size();
}

// Split a markdown document into the preamble (everything before the first
// "## " heading, e.g. "# Spec") and the ordered list of sections. Fenced code
// blocks are copied verbatim: a "## " line inside them is not a heading.
void parse_sections(const std::string& content, std::string* preamble, std::vector<Section>* sections) {
    std::vector<std::string> lines;
    {
        std::istringstream iss(content);
        std::string line;
        while (std::getline(iss, line)) lines.push_back(line);
    }

    std::string collected;
    Section current{"", ""};
    bool in_section = false;

    auto flush = [&]() {
        if (!in_section) return;
        current.body = trim_block(current.body);
        sections->push_back(current);
        current = Section{"", ""};
    };
    auto append = [&](const std::string& line) {
        if (in_section) {
            current.body += line;
            current.body += '\n';
        } else {
            collected += line;
            collected += '\n';
        }
    };

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.rfind("## ", 0) == 0) {
            flush();
            in_section = true;
            current.title = trim(line.substr(3));
            continue;
        }

        std::string marker;
        if (fence_open(line, &marker)) {
            size_t close_index = lines.size();
            for (size_t j = i + 1; j < lines.size(); ++j) {
                if (fence_close(lines[j], marker)) {
                    close_index = j;
                    break;
                }
            }
            if (close_index < lines.size()) {
                for (size_t j = i; j <= close_index; ++j) append(lines[j]);
                i = close_index;
                continue;
            }
        }
        append(line);
    }
    flush();
    *preamble = collected;
}

std::string render_document(std::string preamble, const std::vector<Section>& sections) {
    std::string out = trim_block(preamble);
    if (out.empty()) out = "# Spec";
    for (const auto& section : sections) {
        out += "\n\n## " + section.title;
        if (!section.body.empty()) out += "\n" + section.body;
    }
    out += "\n";
    return out;
}

bool titles_match(const std::string& design_title, const std::string& spec_title) {
    if (design_title.empty() || spec_title.empty()) return false;
    return spec_title.find(design_title) != std::string::npos ||
           design_title.find(spec_title) != std::string::npos;
}

std::string active_change_value(const std::string& state_content) {
    size_t pos = state_content.find(kActiveChange);
    if (pos == std::string::npos) return "";
    size_t value_begin = pos + std::string(kActiveChange).size();
    size_t line_end = state_content.find('\n', value_begin);
    if (line_end == std::string::npos) line_end = state_content.size();
    return trim(state_content.substr(value_begin, line_end - value_begin));
}

bool has_state_field(const std::string& content, const std::string& key) {
    size_t pos = 0;
    while (pos < content.size()) {
        size_t line_end = content.find('\n', pos);
        if (line_end == std::string::npos) line_end = content.size();
        if (content.compare(pos, key.size(), key) == 0) return true;
        pos = line_end + 1;
    }
    return false;
}

std::string set_active_change(const std::string& state_content, const std::string& value) {
    size_t pos = state_content.find(kActiveChange);
    if (pos == std::string::npos) {
        std::string out = state_content;
        if (!out.empty() && out.back() != '\n') out += '\n';
        return out + "\nActive Change: " + value + "\n";
    }
    size_t line_end = state_content.find('\n', pos);
    if (line_end == std::string::npos) line_end = state_content.size();
    return state_content.substr(0, pos) + "Active Change: " + value + state_content.substr(line_end);
}

std::string join(const std::vector<std::string>& values, const std::string& separator, const std::string& empty_text) {
    if (values.empty()) return empty_text;
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        out += (i == 0 ? "" : separator) + values[i];
    }
    return out;
}

} // namespace

void register_close(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    struct Args {
        std::string id;
        bool dry_run = false;
    };
    auto args = std::make_shared<Args>();

    auto* cmd_close = app.add_subcommand("close", "Close a change: fold design.md into SPEC.md");
    add_global_opts(cmd_close, global_opts);
    cmd_close->add_option("id", args->id, "Change ID to close")->required();
    cmd_close->add_flag("--dry-run", args->dry_run, "Preview the fold without writing anything");
    cmd_close->footer("Example:\n  sele close 20261018-my-feature --dry-run\n  sele close 20261018-my-feature");
    cmd_close->callback([&global_opts, &exit_code, args]() {
        util::Output output(global_opts.output_format);
        try {
            fs::path root = global_opts.root.empty() ? fs::current_path() : global_opts.root;
            fs::path design_root = core::find_design_root(root);
            fs::path agent_root = core::find_agent_root(root);

            fs::path changes_dir = design_root / ".design" / "changes" / args->id;
            fs::path work_dir = agent_root / ".agent" / "work" / args->id;

            if (!fs::exists(changes_dir) || !fs::exists(work_dir)) {
                output.error("Both .design/changes/<id> and .agent/work/<id> must exist",
                             "sele status    # check the active id");
                exit_code = exit_codes::STATE_CONFLICT;
                return;
            }

            fs::path design_md = changes_dir / "design.md";
            if (!fs::exists(design_md)) {
                output.error("Missing design.md in " + changes_dir.string(), "sele lint --fix");
                exit_code = exit_codes::STATE_CONFLICT;
                return;
            }

            std::string design_preamble;
            std::vector<Section> design_sections;
            parse_sections(read_file(design_md), &design_preamble, &design_sections);

            fs::path spec_md = design_root / ".design" / "SPEC.md";
            std::string spec_preamble;
            std::vector<Section> spec_sections;
            if (fs::exists(spec_md)) {
                parse_sections(read_file(spec_md), &spec_preamble, &spec_sections);
            }

            std::vector<std::string> design_titles;
            std::vector<std::string> spec_titles;
            for (const auto& section : design_sections) design_titles.push_back(section.title);
            for (const auto& section : spec_sections) spec_titles.push_back(section.title);

            // Plan the fold: matched sections revise the spec in place, the rest
            // are appended as new sections, empty sections are skipped. Matching
            // runs against `folded`, not the original spec, so a title repeated
            // inside one design.md merges instead of duplicating a section.
            std::vector<Section> folded = spec_sections;
            std::vector<std::string> plan_section;
            std::vector<std::string> plan_action;
            std::vector<std::string> plan_target;
            nlohmann::json suggested_mapping = nlohmann::json::object();
            for (const auto& section : design_sections) {
                long match = -1;
                for (size_t i = 0; i < folded.size(); ++i) {
                    if (titles_match(section.title, folded[i].title)) {
                        match = static_cast<long>(i);
                        break;
                    }
                }

                std::string action;
                std::string target;
                if (!has_content(section.body)) {
                    action = "skipped";
                    target = match >= 0 ? folded[static_cast<size_t>(match)].title : "";
                } else if (match >= 0) {
                    action = "replaced";
                    folded[static_cast<size_t>(match)].body = section.body;
                    target = folded[static_cast<size_t>(match)].title;
                } else {
                    action = "appended";
                    folded.push_back(section);
                    target = "";
                }

                suggested_mapping[section.title] = target;
                plan_section.push_back(section.title);
                plan_action.push_back(action);
                plan_target.push_back(target);
            }

            if (args->dry_run) {
                nlohmann::json plan = nlohmann::json::array();
                for (size_t i = 0; i < plan_section.size(); ++i) {
                    plan.push_back({{"section", plan_section[i]}, {"action", plan_action[i]}});
                }

                nlohmann::json result;
                result["dry_run"] = true;
                result["id"] = args->id;
                result["design_sections"] = design_titles;
                result["spec_sections"] = spec_titles;
                result["suggested_mapping"] = suggested_mapping;
                result["plan"] = plan;

                std::ostringstream text;
                text << "Close preview for " << args->id << " (dry run, nothing written)\n"
                     << "  design sections: " << join(design_titles, ", ", "(none)") << '\n'
                     << "  spec sections: " << join(spec_titles, ", ", "(none)") << '\n'
                     << "  planned fold:";
                for (size_t i = 0; i < plan_section.size(); ++i) {
                    text << "\n    " << plan_section[i] << " -> " << plan_action[i];
                    if (plan_action[i] == "replaced") text << " (spec section: " << plan_target[i] << ")";
                }
                if (plan_section.empty()) text << "\n    (design.md has no ## sections)";

                output.print_both(text.str(), result);
                exit_code = exit_codes::SUCCESS;
                return;
            }

            // Only touch SPEC.md when the fold actually changes the rendered
            // document, so re-closing an already folded change does not rewrite
            // the file (and normalize its line endings).
            std::string spec_before = render_document(spec_preamble, spec_sections);
            std::string spec_after = render_document(spec_preamble, folded);
            bool spec_updated = spec_after != spec_before;
            if (spec_updated) {
                write_file(spec_md, spec_after);
            }

            fs::path state_md = agent_root / ".agent" / "STATE.md";
            std::string state_value;
            bool state_fields_present = false;
            bool state_updated = false;
            if (fs::exists(state_md)) {
                std::string state_content = read_file(state_md);
                state_value = active_change_value(state_content);
                state_fields_present = has_state_field(state_content, "Phase:") ||
                                       has_state_field(state_content, "Blocked:") ||
                                       has_state_field(state_content, "Next:");
                if (state_value == args->id) {
                    write_file(state_md, set_active_change(state_content, "无"));
                    state_updated = true;
                }
            }

            nlohmann::json fold_result = nlohmann::json::array();
            for (size_t i = 0; i < plan_section.size(); ++i) {
                fold_result.push_back({{"section", plan_section[i]}, {"action", plan_action[i]}});
            }

            nlohmann::json result;
            result["dry_run"] = false;
            result["id"] = args->id;
            result["spec_path"] = spec_md.string();
            result["spec_updated"] = spec_updated;
            result["folded"] = fold_result;
            result["state_updated"] = state_updated;

            std::ostringstream text;
            text << "Closed " << args->id << " (spec folded, directories kept)\n"
                 << "  spec: " << spec_md.string() << (spec_updated ? "" : " (unchanged)") << '\n'
                 << "  folded:";
            for (size_t i = 0; i < plan_section.size(); ++i) {
                text << "\n    " << plan_section[i] << " -> " << plan_action[i];
            }
            if (plan_section.empty()) text << "\n    (design.md has no ## sections)";
            text << "\n  state: ";
            if (state_updated) {
                text << "Active Change -> 无";
            } else if (state_value.empty()) {
                text << "Active Change untouched (no active pointer)";
            } else {
                text << "Active Change untouched (points to " << state_value << ")";
            }
            if (state_fields_present) {
                text << " (Phase/Blocked/Next left as-is)";
            }

            output.print_both(text.str(), result);
            exit_code = exit_codes::SUCCESS;
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });
}

} // namespace cli
