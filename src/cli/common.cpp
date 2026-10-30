#include "cli/common.hpp"

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
    return ctx;
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
