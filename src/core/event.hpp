#pragma once

#include "core/entity.hpp"
#include "core/store.hpp"

#include <vector>

namespace core::event {

void append(const store::Paths& paths, const model::Event& event);
std::vector<model::Event> load(const store::Paths& paths);

} // namespace core::event
