#include "workshop_dialog.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>
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

QString dependency_text(const char* label) {
  return QCoreApplication::translate("RecipeGraph", label);
}

QTreeWidgetItem* dependency(QTreeWidgetItem* parent, const QString& text) {
  return new QTreeWidgetItem(parent, QStringList{text});
}

void recipe_links(QTreeWidgetItem* parent, const QJsonArray& recipes) {
  for (const auto recipe : recipes) {
    const auto id = recipe.toString();
    auto* link =
        dependency(parent, dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "Recipe %1")).arg(id));
    link->setData(0, Qt::UserRole, id);
  }
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

void show_recipe_graph(QWidget* parent, const QJsonObject& graph) {
  QDialog dialog(parent);
  dialog.setObjectName("recipeGraph");
  const auto title = dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "Crafting Dependencies"));
  dialog.setWindowTitle(title);
  dialog.resize(620, 440);
  auto* layout = new QVBoxLayout(&dialog);
  theme::configurePanelLayout(*layout);
  const auto mode = graph.value("mode").toString();
  auto* form = new QFormLayout;
  layout->addLayout(form);
  auto* select = new QComboBox(&dialog);
  select->setObjectName("dependencyRecipe");
  const auto recipes = graph.value("recipes").toObject();
  const auto products = graph.value("items").toObject();
  const auto unlocks = graph.value("station_unlocks").toObject();
  auto* status = new QLabel(&dialog);
  status->setObjectName("dependencyStatus");
  form->addRow(dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "Recipe")), select);
  layout->addWidget(status);
  auto* tree = new QTreeWidget(&dialog);
  tree->setObjectName("dependencyTree");
  tree->setAccessibleName(title);
  tree->setHeaderHidden(true);
  layout->addWidget(tree, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  layout->addWidget(buttons);
  dialog.connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog.connect(tree, &QTreeWidget::itemActivated, &dialog, [select](QTreeWidgetItem* item) {
    const auto index = select->findText(item->data(0, Qt::UserRole).toString());
    if (index >= 0) select->setCurrentIndex(index);
  });
  const auto fill = [&] {
    tree->clear();
    if (select->currentIndex() < 0) {
      status->setText(
          dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "No recipes in this project.")));
      return;
    }
    const auto recipe = recipes.value(select->currentText()).toObject();
    status->setText((recipe.value("command_allowed").toBool()
                         ? dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "%1 · %2 permitted"))
                         : dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "%1 · %2 disabled")))
                        .arg(mode, recipe.value("method").toString()));
    const auto output = recipe.value("output").toObject();
    new QTreeWidgetItem(
        tree, QStringList{dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "Produces %1 × %2"))
                              .arg(output.value("id").toString())
                              .arg(output.value("count").toInt())});
    const auto station = recipe.value("station").toString();
    if (!station.isEmpty()) {
      const bool unlocked = graph.value("unlocked_stations").toArray().contains(station);
      auto* node = new QTreeWidgetItem(
          tree,
          QStringList{
              (unlocked
                   ? dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "Station %1 · Available"))
                   : dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "Station %1 · Locked")))
                  .arg(station)});
      recipe_links(node, unlocks.value(station).toArray());
    }
    const auto granted = recipe.value("unlocks").toString();
    if (!granted.isEmpty())
      new QTreeWidgetItem(
          tree, QStringList{
                    dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "Unlocks %1")).arg(granted)});
    const bool has_pattern = recipe.contains("pattern");
    const auto materials = recipe.value(has_pattern ? "pattern" : "ingredients").toArray();
    auto* inputs = new QTreeWidgetItem(
        tree, QStringList{has_pattern
                              ? dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "Pattern slots"))
                              : dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "Ingredients"))});
    for (const auto material : materials) {
      if (material.isNull()) {
        dependency(inputs, dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", "Empty slot")));
        continue;
      }
      const auto part = material.toObject();
      const auto selector = part.value("selector").toString();
      auto* node = dependency(
          inputs, QStringLiteral("%1 × %2").arg(selector).arg(part.value("count").toInt()));
      for (auto it = products.begin(); it != products.end(); ++it) {
        const auto product = it.value().toObject();
        const bool tag = selector.startsWith(u'#');
        if (tag ? !product.value("tags").toArray().contains(selector.mid(1)) : it.key() != selector)
          continue;
        auto* item = tag ? dependency(node, it.key()) : node;
        recipe_links(item, product.value("recipes").toArray());
        for (const auto location : product.value("sources").toArray()) {
          const auto site = location.toObject();
          auto label = QStringLiteral("%1 / %2").arg(site.value("region").toString(),
                                                     site.value("id").toString());
          if (site.contains("requires_item"))
            label += dependency_text(QT_TRANSLATE_NOOP("RecipeGraph", " · Requires %1"))
                         .arg(site.value("requires_item").toString());
          dependency(item, label);
        }
      }
    }
    tree->expandToDepth(2);
  };
  select->addItems(recipes.keys());
  select->setEnabled(select->count() > 0);
  dialog.connect(select, &QComboBox::currentIndexChanged, &dialog, fill);
  fill();
  dialog.exec();
}

}  // namespace wds::studio
