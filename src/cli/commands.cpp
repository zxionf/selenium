#include "cli/commands.hpp"

#include <map>
#include <string>

void add_output_opt(CLI::App* sub, GlobalOptions& global_opts) {
    sub->add_option("--output", global_opts.output_format, "Output format: text or json")
        ->transform(CLI::CheckedTransformer(std::map<std::string, util::Output::Format>{
            {"text", util::Output::Format::TEXT},
            {"json", util::Output::Format::JSON}
        }, CLI::ignore_case))
        ->capture_default_str();
}

void add_global_opts(CLI::App* sub, GlobalOptions& global_opts) {
    sub->add_option("--root", global_opts.root, "Project root directory")
        ->capture_default_str();
    add_output_opt(sub, global_opts);
}
