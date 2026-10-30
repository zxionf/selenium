#pragma once

#include <string>

namespace core::state {

// Transition whitelists from docs/sele-f.md section 4.
bool plan_transition(const std::string& from, const std::string& to);
bool line_transition(const std::string& from, const std::string& to);
bool task_transition(const std::string& from, const std::string& to);
bool step_transition(const std::string& from, const std::string& to);

} // namespace core::state
