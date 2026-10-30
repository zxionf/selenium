#pragma once

#include "core/entity.hpp"
#include "core/store.hpp"

#include <string>

namespace core::checkpoint {

std::string next_id(const store::Paths& paths, const std::string& ts);
model::Checkpoint create(const store::Paths& paths, const model::Task& task,
                         const model::Step& step);
model::Checkpoint create_task(const store::Paths& paths, const model::Task& task);

} // namespace core::checkpoint
