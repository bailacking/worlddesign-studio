#include "workshop_dialog.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

#include "theme/theme.hpp"

namespace wds::studio {
namespace {

QString text(std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString action_label(const items::Workshop& workshop, const items::Action& action) {
  const auto id = text(action.id);
  if (action.type == "gather")
    return QCoreApplication::translate("WorkshopDialog", "Gather %1").arg(id);
  if (action.type == "pickup")
    return QCoreApplication::translate("WorkshopDialog", "Pick up %1").arg(id);
  if (action.type == "store")
    return QCoreApplication::translate("WorkshopDialog", "Store one %1").arg(id);
  if (action.type == "take")
    return QCoreApplication::translate("WorkshopDialog", "Take one %1").arg(id);
  const auto& recipe = workshop.catalog().recipes.at(action.id);
  if (recipe.method == "smelt")
    return QCoreApplication::translate("WorkshopDialog", "Smelt %1").arg(id);
  if (recipe.method == "cook")
    return QCoreApplication::translate("WorkshopDialog", "Cook %1").arg(id);
  return QCoreApplication::translate("WorkshopDialog", "Craft %1").arg(id);
}

QString receipt(std::string_view code) {
  if (code == "ok") return QCoreApplication::translate("WorkshopDialog", "Done.");
  if (code == "source_out_of_reach")
    return QCoreApplication::translate("WorkshopDialog", "Move closer to the source.");
  if (code == "source_empty")
    return QCoreApplication::translate("WorkshopDialog", "Source depleted.");
  if (code == "container_full")
    return QCoreApplication::translate("WorkshopDialog", "Container full.");
  if (code == "station_locked")
    return QCoreApplication::translate("WorkshopDialog", "Station locked.");
  if (code == "missing_ingredients")
    return QCoreApplication::translate("WorkshopDialog", "Missing ingredients.");
  return text(code);
}

QString recipe_detail(const items::Recipe& recipe) {
  const auto describe = [](const items::Ingredient& part) {
    return QString("%1 × %2").arg(text(part.selector)).arg(part.count);
  };
  QStringList parts;
  for (const auto& part : recipe.ingredients) parts.append(describe(part));
  if (!recipe.pattern.empty()) {
    for (const auto& part : recipe.pattern) parts.append(part ? describe(*part) : QString("–"));
  }
  QString detail = QCoreApplication::translate("WorkshopDialog", "%1 → %2 × %3")
                       .arg(parts.join(" / "), text(recipe.output.id))
                       .arg(recipe.output.count);
  if (!recipe.requires_station.empty())
    detail += "\n" + QCoreApplication::translate("WorkshopDialog", "Requires %1")
                         .arg(text(recipe.requires_station));
  return detail;
}

}  // namespace

bool show_workshop(QWidget* parent, const items::Workshop& workshop, bool gathering,
                   const std::function<std::string_view(const items::Action&)>& apply) {
  QDialog dialog(parent);
  dialog.setObjectName("workshopDialog");
  dialog.setWindowTitle(QCoreApplication::translate("WorkshopDialog", "Items and recipes"));
  dialog.resize(610, 380);
  auto* root = new QVBoxLayout(&dialog);
  theme::configurePanelLayout(*root);
  auto* columns = new QHBoxLayout;
  root->addLayout(columns, 1);
  const auto add_inventory = [&](const QString& heading) {
    auto* layout = new QVBoxLayout;
    columns->addLayout(layout, 1);
    layout->addWidget(new QLabel(heading, &dialog));
    auto* list = new QListWidget(&dialog);
    list->setAccessibleName(heading);
    layout->addWidget(list, 1);
    return list;
  };
  auto* bag = add_inventory(QCoreApplication::translate("WorkshopDialog", "Bag"));
  auto* chest = add_inventory(QCoreApplication::translate("WorkshopDialog", "Chest"));
  auto* loot = add_inventory(QCoreApplication::translate("WorkshopDialog", "Drops"));
  auto* form = new QFormLayout;
  root->addLayout(form);
  auto* select = new QComboBox(&dialog);
  select->setObjectName("workshopAction");
  form->addRow(QCoreApplication::translate("WorkshopDialog", "Action"), select);
  auto* requirements = new QLabel(&dialog);
  requirements->setWordWrap(true);
  root->addWidget(requirements);
  auto* status = new QLabel(&dialog);
  status->setObjectName("workshopStatus");
  root->addWidget(status);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  root->addWidget(buttons);
  auto* run = buttons->addButton(QCoreApplication::translate("WorkshopDialog", "Run"),
                                 QDialogButtonBox::ActionRole);
  run->setObjectName("workshopRun");
  run->setDefault(true);
  dialog.connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto actions = workshop.actions(gathering);
  bool changed = false;
  const auto selected = [&]() -> const items::Action* {
    const int index = select->currentIndex();
    return index < 0 ? nullptr : &actions[static_cast<std::size_t>(index)];
  };
  const auto display_requirements = [&] {
    const auto* action = selected();
    requirements->setText(action && action->type == "recipe"
                              ? recipe_detail(workshop.catalog().recipes.at(action->id))
                              : QString{});
  };
  const auto refresh = [&](const items::Action& previous) {
    const auto populate = [](QListWidget& list, const items::Container& container) {
      list.clear();
      for (const auto& slot : container)
        list.addItem(QString("%1 × %2").arg(text(slot.id)).arg(slot.count));
    };
    const auto& state = workshop.state();
    populate(*bag, state.bag);
    populate(*chest, state.chest);
    populate(*loot, state.loot);
    actions = workshop.actions(gathering);
    select->blockSignals(true);
    select->clear();
    for (const auto& action : actions) select->addItem(action_label(workshop, action));
    const auto match = std::ranges::find(actions, previous);
    if (match != actions.end()) select->setCurrentIndex(static_cast<int>(match - actions.begin()));
    select->blockSignals(false);
    display_requirements();
    run->setEnabled(!actions.empty());
  };
  dialog.connect(select, &QComboBox::currentIndexChanged, &dialog, display_requirements);
  dialog.connect(run, &QPushButton::clicked, &dialog, [&] {
    if (const auto* action = selected()) {
      const auto previous = *action;
      const auto code = apply(previous);
      changed = changed || code == "ok";
      status->setText(receipt(code));
      refresh(previous);
    }
  });
  refresh({});
  select->setFocus();
  dialog.exec();
  return changed;
}

}  // namespace wds::studio
