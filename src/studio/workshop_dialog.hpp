#pragma once

#include <functional>

#include "workshop.hpp"

class QWidget;

namespace wds::studio {

bool show_workshop(QWidget* parent, const items::Workshop& workshop, bool gathering,
                   const std::function<std::string_view(const items::Action&)>& apply);

}  // namespace wds::studio
