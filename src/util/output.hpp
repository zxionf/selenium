#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace util {

class Output {
public:
    enum class Format { TEXT, JSON };

    explicit Output(Format format = Format::TEXT) : format_(format) {}

    void print_text(const std::string& message);
    void print_json(const nlohmann::json& json);
    void print_both(const std::string& text, const nlohmann::json& json);
    void error(const std::string& message, const std::string& suggestion = "");

    Format get_format() const { return format_; }

private:
    Format format_;
};

} // namespace util