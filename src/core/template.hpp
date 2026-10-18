#pragma once

#include <string>

namespace core {

std::string render_design_template(const std::string& id);
std::string render_tasks_template(const std::string& id);
std::string render_decisions_header();
std::string render_decision_entry(int index, const std::string& change_id,
                                  const std::string& context,
                                  const std::string& decision,
                                  const std::string& consequences);

} // namespace core