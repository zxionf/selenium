#include "cli/common.hpp"

#include "core/config.hpp"
#include "core/entity.hpp"
#include "core/event.hpp"
#include "core/git.hpp"
#include "util/color.hpp"

#include <sstream>

void add_global_opts(CLI::App* app, GlobalOptions& options) {
    app->add_option("--root", options.root, "Repository root")->default_val(".");
    app->add_option("--output", options.output, "Output format: text|json")
        ->default_val("text")
        ->check(CLI::IsMember({"text", "json"}));
}

std::shared_ptr<Context> make_context(const GlobalOptions& options) {
    auto ctx = std::make_shared<Context>();
    ctx->paths = core::store::make_paths(options.root);
    ctx->out = util::Output(options.output == "json" ? util::Output::Format::JSON
                                                     : util::Output::Format::TEXT);
    const auto config = core::config::load(ctx->paths);
    if (config.color == "always") {
        util::color::configure(util::color::Mode::ALWAYS);
    } else if (config.color == "never") {
        util::color::configure(util::color::Mode::NEVER);
    } else {
        util::color::configure(util::color::Mode::AUTO);
    }
    const auto drift = core::git::detect_drift(ctx->paths);
    ctx->drift = static_cast<int>(drift.kind);
    if (drift.kind != core::git::DriftKind::NONE) {
        ctx->drift_message = core::git::kind_name(drift.kind) + " " + drift.old_head + " -> " +
                             drift.new_head;
    }
    return ctx;
}

TaskRef parse_task_ref(const std::string& raw, const std::string& plan,
                       const std::string& line) {
    TaskRef ref{plan, line, raw};
    const auto first = raw.find('.');
    if (first == std::string::npos) return ref;
    const auto second = raw.find('.', first + 1);
    if (second == std::string::npos) {
        ref.line = raw.substr(0, first);
        ref.id = raw.substr(first + 1);
    } else {
        ref.plan = raw.substr(0, first);
        ref.line = raw.substr(first + 1, second - first - 1);
        ref.id = raw.substr(second + 1);
    }
    return ref;
}

std::vector<std::string> split_csv(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

void fail(Context& ctx, int& exit_code, int code, const std::string& message,
          const std::string& suggestion) {
    ctx.out.error(message, suggestion);
    exit_code = code;
}

void append_event(const core::store::Paths& paths, const std::string& actor,
                  const std::string& op, const nlohmann::json& entity,
                  const nlohmann::json& before, const nlohmann::json& after,
                  const std::string& reason) {
    core::model::Event event;
    event.ts = core::model::now_utc();
    event.actor = actor;
    event.op = op;
    event.entity = entity;
    event.before = before;
    event.after = after;
    event.reason = reason;
    core::event::append(paths, event);
}
