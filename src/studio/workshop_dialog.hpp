#pragma once

#include <functional>

#include "workshop.hpp"

class QWidget;
class QJsonObject;

namespace wds::studio {

bool show_workshop(QWidget* parent, const items::Workshop& workshop, bool gathering,
                   const std::function<std::string_view(const items::Action&)>& apply);
void show_recipe_graph(QWidget* parent, const QJsonObject& graph);

}  // namespace wds::studio
