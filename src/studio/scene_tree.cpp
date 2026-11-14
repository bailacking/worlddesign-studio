#include "scene_tree.hpp"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <utility>

namespace wds::studio {
namespace {

// 首批文案为英文，与现有 Win32 工作台一致；WKB-12 接入翻译时按此上下文取条目。
QString translated(const char* text) {
  return QCoreApplication::translate("wds::studio::SceneTreePanel", text);
}

}  // namespace

SceneTreePanel::SceneTreePanel(std::function<void(QString)> onSelect, QWidget* parent)
    : QWidget(parent), on_select_(std::move(onSelect)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);

  search_ = new QLineEdit(this);
  search_->setPlaceholderText(translated("Search entities"));
  search_->setClearButtonEnabled(true);
  search_->setAccessibleName(translated("Entity search"));
  search_->installEventFilter(this);
  layout->addWidget(search_);

  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(1);
  tree_->setHeaderHidden(true);
  tree_->setRootIsDecorated(false);  // 层级待场景数据模型提供，首批按输入顺序平铺
  tree_->setUniformRowHeights(true);
  tree_->setAllColumnsShowFocus(true);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  tree_->setTextElideMode(Qt::ElideMiddle);
  tree_->setFocusPolicy(Qt::StrongFocus);
  tree_->setAccessibleName(translated("Scene entities"));
  tree_->installEventFilter(this);

  empty_state_ = new QLabel(this);
  empty_state_->setAlignment(Qt::AlignCenter);
  empty_state_->setWordWrap(true);
  empty_state_->setTextInteractionFlags(Qt::NoTextInteraction);
  empty_state_->setProperty("state", "empty");  // WKB-02 主题的次要文本色

  stack_ = new QStackedWidget(this);
  stack_->addWidget(tree_);
  stack_->addWidget(empty_state_);
  layout->addWidget(stack_, 1);

  setFocusProxy(tree_);

  connect(search_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
  connect(tree_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* current) { handleCurrentChanged(current); });

  updateEmptyState();
}

void SceneTreePanel::setEntities(const QJsonArray& entities) {
  QSet<QString> seen;
  updating_ = true;
  tree_->clear();
  for (const QJsonValue& value : entities) {
    if (!value.isObject()) continue;
    const QJsonValue id = value.toObject().value(QStringLiteral("id"));
    if (!id.isString()) continue;
    const QString stable_id = id.toString();
    // 稳定 ID 唯一：空 ID 和重复 ID 不上列表，保证回调值能唯一指回实体。
    if (stable_id.isEmpty() || seen.contains(stable_id)) continue;
    seen.insert(stable_id);
    auto* row = new QTreeWidgetItem(tree_);
    row->setText(0, stable_id);
    row->setData(0, Qt::UserRole, stable_id);
    row->setToolTip(0, stable_id);  // 长 ID 悬停可读全文
  }
  updating_ = false;

  if (tree_->topLevelItemCount() == 0) {
    // 空数组表示关闭项目：清空逻辑选中，不回调。
    selected_id_.clear();
    const QSignalBlocker blocker(search_);
    search_->clear();
  }
  applyFilter();
}

void SceneTreePanel::setSelectedId(const QString& id) {
  selected_id_ = id;
  syncVisibleSelection();
}

QString SceneTreePanel::selectedId() const { return selected_id_; }

bool SceneTreePanel::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    // Esc 是用户主动清空选择，按约定回传空字符串；程序化清空不走这里。
    if (key->key() == Qt::Key_Escape) {
      clearSelectionByUser();
      return true;
    }
    // 搜索框里按上下键直接进入列表，避免只能用 Tab 切换。
    if (watched == search_ && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
      focusTreeFromSearch();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void SceneTreePanel::applyFilter() {
  const QString term = search_->text().trimmed();
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    QTreeWidgetItem* row = tree_->topLevelItem(index);
    row->setHidden(!term.isEmpty() && !row->text(0).contains(term, Qt::CaseInsensitive));
  }
  // 选中项被过滤掉时只隐藏高亮，逻辑选中 ID 仍保留，清除过滤后自动恢复。
  syncVisibleSelection();
  updateEmptyState();
}

void SceneTreePanel::updateEmptyState() {
  if (tree_->topLevelItemCount() == 0) {
    empty_state_->setText(translated("This scene has no entities."));
    stack_->setCurrentWidget(empty_state_);
    return;
  }
  if (!hasVisibleRow()) {
    empty_state_->setText(translated("No entity matches the search."));
    stack_->setCurrentWidget(empty_state_);
    return;
  }
  stack_->setCurrentWidget(tree_);
}

void SceneTreePanel::handleCurrentChanged(QTreeWidgetItem* current) {
  if (updating_) return;  // 程序化改动 currentItem：同步选中，不回调
  adoptUserSelection(current != nullptr ? current->data(0, Qt::UserRole).toString() : QString());
}

void SceneTreePanel::syncVisibleSelection() {
  updating_ = true;
  QTreeWidgetItem* row = findRow(selected_id_);
  if (row != nullptr && !row->isHidden()) {
    tree_->setCurrentItem(row);
    tree_->scrollToItem(row);
  } else {
    tree_->setCurrentItem(nullptr);
  }
  updating_ = false;
}

void SceneTreePanel::adoptUserSelection(const QString& id) {
  if (id == selected_id_) return;  // 同一选中不重复回调
  selected_id_ = id;
  if (on_select_) on_select_(id);
}

void SceneTreePanel::clearSelectionByUser() {
  if (selected_id_.isEmpty() && tree_->currentItem() == nullptr) return;
  updating_ = true;
  tree_->setCurrentItem(nullptr);
  tree_->clearSelection();
  updating_ = false;
  adoptUserSelection(QString());
}

void SceneTreePanel::focusTreeFromSearch() {
  if (!hasVisibleRow()) return;
  QTreeWidgetItem* target = tree_->currentItem();
  if (target == nullptr || target->isHidden()) target = firstVisibleRow();
  tree_->setFocus(Qt::ShortcutFocusReason);
  if (target != nullptr && target != tree_->currentItem()) tree_->setCurrentItem(target);
}

QTreeWidgetItem* SceneTreePanel::findRow(const QString& id) const {
  if (id.isEmpty()) return nullptr;
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    QTreeWidgetItem* row = tree_->topLevelItem(index);
    if (row->data(0, Qt::UserRole).toString() == id) return row;
  }
  return nullptr;
}

QTreeWidgetItem* SceneTreePanel::firstVisibleRow() const {
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    QTreeWidgetItem* row = tree_->topLevelItem(index);
    if (!row->isHidden()) return row;
  }
  return nullptr;
}

bool SceneTreePanel::hasVisibleRow() const { return firstVisibleRow() != nullptr; }

}  // namespace wds::studio
