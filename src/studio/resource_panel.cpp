#include "resource_panel.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <utility>

namespace wds::studio {
namespace {

QString displayKind(const QString& kind) {
  if (kind == "scene") return QCoreApplication::translate("ResourcePanel", "Scene");
  if (kind == "data") return QCoreApplication::translate("ResourcePanel", "Data");
  return kind;
}

constexpr int kIdColumn = 0;
constexpr int kKindColumn = 1;
constexpr int kPathColumn = 2;
// 每条资源的 refs 存在 ID 列的附加角色上，引用查看与反查都从这里取，避免另建索引。
constexpr int kRefsRole = Qt::UserRole + 1;

}  // namespace

ResourcePanel::ResourcePanel(std::function<void(QString)> onSelect, QWidget* parent)
    : QWidget(parent), on_select_(std::move(onSelect)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);

  search_ = new QLineEdit(this);
  search_->setClearButtonEnabled(true);
  search_->installEventFilter(this);
  layout->addWidget(search_);

  kind_filter_ = new QComboBox(this);
  layout->addWidget(kind_filter_);

  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(3);
  tree_->setRootIsDecorated(false);
  tree_->setUniformRowHeights(true);
  tree_->setAllColumnsShowFocus(true);
  tree_->setWordWrap(false);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  tree_->setTextElideMode(Qt::ElideMiddle);
  tree_->setFocusPolicy(Qt::StrongFocus);
  tree_->installEventFilter(this);
  // 路径列吃掉剩余宽度；省略号由 ElideMiddle 给出，完整路径在悬停提示里。
  tree_->header()->setSectionResizeMode(kIdColumn, QHeaderView::Interactive);
  tree_->header()->resizeSection(kIdColumn, 130);
  tree_->header()->setSectionResizeMode(kKindColumn, QHeaderView::ResizeToContents);
  tree_->header()->setSectionResizeMode(kPathColumn, QHeaderView::Stretch);

  empty_state_ = new QLabel(this);
  empty_state_->setAlignment(Qt::AlignCenter);
  empty_state_->setWordWrap(true);
  empty_state_->setTextInteractionFlags(Qt::NoTextInteraction);
  empty_state_->setProperty("state", "empty");  // WKB-02 主题的次要文本色

  stack_ = new QStackedWidget(this);
  stack_->addWidget(tree_);
  stack_->addWidget(empty_state_);
  layout->addWidget(stack_, 1);

  // 引用区放在列表下方：两张只读标签，无选中时整块隐藏。
  relations_ = new QWidget(this);
  auto* relations_layout = new QGridLayout(relations_);
  relations_layout->setContentsMargins(0, 0, 0, 0);
  relations_layout->setHorizontalSpacing(8);
  relations_layout->setVerticalSpacing(2);
  references_caption_ = new QLabel(relations_);
  backlinks_caption_ = new QLabel(relations_);
  for (QLabel* caption : {references_caption_, backlinks_caption_}) {
    caption->setProperty("state", "empty");  // 次要文本色
    caption->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  }
  references_value_ = new QLabel(relations_);
  backlinks_value_ = new QLabel(relations_);
  references_value_->setObjectName(QStringLiteral("resourceReferences"));
  backlinks_value_->setObjectName(QStringLiteral("resourceBacklinks"));
  for (QLabel* value : {references_value_, backlinks_value_}) {
    value->setWordWrap(true);
    // 只读：可选中复制 ID，但不能编辑。
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
  }
  relations_layout->addWidget(references_caption_, 0, 0);
  relations_layout->addWidget(references_value_, 0, 1);
  relations_layout->addWidget(backlinks_caption_, 1, 0);
  relations_layout->addWidget(backlinks_value_, 1, 1);
  relations_layout->setColumnStretch(1, 1);
  layout->addWidget(relations_);
  relations_->hide();

  setFocusProxy(tree_);

  connect(search_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
  connect(kind_filter_, &QComboBox::currentIndexChanged, this, [this] { applyFilter(); });
  connect(tree_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* current) { handleCurrentChanged(current); });

  retranslate();
}

void ResourcePanel::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) retranslate();
}

void ResourcePanel::retranslate() {
  search_->setPlaceholderText(QCoreApplication::translate("ResourcePanel", "Search ID or path"));
  search_->setAccessibleName(QCoreApplication::translate("ResourcePanel", "Resource search"));
  kind_filter_->setAccessibleName(
      QCoreApplication::translate("ResourcePanel", "Resource type filter"));
  tree_->setAccessibleName(QCoreApplication::translate("ResourcePanel", "Project resources"));
  tree_->setHeaderLabels({QCoreApplication::translate("ResourcePanel", "ID"),
                          QCoreApplication::translate("ResourcePanel", "Type"),
                          QCoreApplication::translate("ResourcePanel", "Path")});
  references_caption_->setText(QCoreApplication::translate("ResourcePanel", "References"));
  backlinks_caption_->setText(QCoreApplication::translate("ResourcePanel", "Referenced by"));
  references_value_->setAccessibleName(
      QCoreApplication::translate("ResourcePanel", "Selected resource references"));
  backlinks_value_->setAccessibleName(
      QCoreApplication::translate("ResourcePanel", "Resources referencing the selection"));
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    auto* row = tree_->topLevelItem(index);
    row->setText(kKindColumn, displayKind(row->data(kKindColumn, Qt::UserRole).toString()));
  }
  rebuildKindFilter();
  updateEmptyState();
  refreshRelations();  // “None”等文案随语言切换重算
}

void ResourcePanel::setResources(const QJsonArray& resources) {
  QSet<QString> seen;
  updating_ = true;
  tree_->clear();
  for (const QJsonValue& value : resources) {
    if (!value.isObject()) continue;
    const QJsonObject fields = value.toObject();
    const QJsonValue id = fields.value(QStringLiteral("id"));
    if (!id.isString()) continue;
    const QString stable_id = id.toString();
    // 稳定 ID 唯一：空 ID 和重复 ID 不上列表，保证回调值能唯一指回资源。
    if (stable_id.isEmpty() || seen.contains(stable_id)) continue;
    seen.insert(stable_id);

    auto* row = new QTreeWidgetItem(tree_);
    row->setText(kIdColumn, stable_id);
    const auto kind = fields.value(QStringLiteral("kind")).toString();
    row->setData(kKindColumn, Qt::UserRole, kind);
    row->setText(kKindColumn, displayKind(kind));
    row->setText(kPathColumn, fields.value(QStringLiteral("path")).toString());
    row->setData(kIdColumn, Qt::UserRole, stable_id);
    row->setToolTip(kIdColumn, stable_id);
    row->setToolTip(kPathColumn, row->text(kPathColumn));  // 长路径悬停给全文
    // 直接引用按声明顺序去重；非字符串与空 ID 不当成引用，避免显示不出处的内容。
    QStringList refs;
    const QJsonArray declared = fields.value(QStringLiteral("refs")).toArray();
    for (const QJsonValue& ref : declared) {
      if (!ref.isString()) continue;
      const QString target = ref.toString();
      if (target.isEmpty() || refs.contains(target)) continue;
      refs.append(target);
    }
    row->setData(kIdColumn, kRefsRole, refs);
  }
  updating_ = false;

  if (tree_->topLevelItemCount() == 0) {
    // 空数组表示关闭项目：清空逻辑选中与过滤条件，均不回调。
    selected_id_.clear();
    const QSignalBlocker blocker(search_);
    search_->clear();
  }
  rebuildKindFilter();
  applyFilter();
  refreshRelations();
}

void ResourcePanel::setSelectedId(const QString& id) {
  selected_id_ = id;
  syncVisibleSelection();
  refreshRelations();
}

QString ResourcePanel::selectedId() const { return selected_id_; }

QStringList ResourcePanel::referencesOf(const QString& id) const {
  const QTreeWidgetItem* row = findRow(id);
  return row != nullptr ? row->data(kIdColumn, kRefsRole).toStringList() : QStringList{};
}

QStringList ResourcePanel::referencedBy(const QString& id) const {
  QStringList result;
  if (id.isEmpty()) return result;
  // 遍历全部行，不看 isHidden：过滤只是显示状态，不影响引用关系。
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    const QTreeWidgetItem* row = tree_->topLevelItem(index);
    const QString owner = row->data(kIdColumn, Qt::UserRole).toString();
    if (owner == id) continue;  // 自引用不列为“被引用”
    if (!row->data(kIdColumn, kRefsRole).toStringList().contains(id)) continue;
    if (!result.contains(owner)) result.append(owner);
  }
  return result;
}

bool ResourcePanel::eventFilter(QObject* watched, QEvent* event) {
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

void ResourcePanel::rebuildKindFilter() {
  const QString previous = kind_filter_->currentData().toString();
  QSet<QString> kinds;
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    const QString kind = tree_->topLevelItem(index)->data(kKindColumn, Qt::UserRole).toString();
    if (!kind.isEmpty()) kinds.insert(kind);
  }
  QStringList ordered = kinds.values();
  ordered.sort(Qt::CaseInsensitive);

  const QSignalBlocker blocker(kind_filter_);
  kind_filter_->clear();
  kind_filter_->addItem(QCoreApplication::translate("ResourcePanel", "All types"), QString());
  for (const QString& kind : ordered) kind_filter_->addItem(displayKind(kind), kind);
  const int restored = kind_filter_->findData(previous);
  kind_filter_->setCurrentIndex(restored >= 0 ? restored : 0);
}

void ResourcePanel::applyFilter() {
  const QString term = search_->text().trimmed();
  const QString kind = kind_filter_->currentData().toString();
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    QTreeWidgetItem* row = tree_->topLevelItem(index);
    const bool kind_ok = kind.isEmpty() || row->data(kKindColumn, Qt::UserRole).toString() == kind;
    const bool term_ok = term.isEmpty() ||
                         row->text(kIdColumn).contains(term, Qt::CaseInsensitive) ||
                         row->text(kPathColumn).contains(term, Qt::CaseInsensitive);
    row->setHidden(!(kind_ok && term_ok));
  }
  // 选中项被过滤掉时只撤可见高亮，逻辑选中 ID 仍保留，清除过滤后自动恢复。
  syncVisibleSelection();
  updateEmptyState();
}

void ResourcePanel::updateEmptyState() {
  if (tree_->topLevelItemCount() == 0) {
    empty_state_->setText(
        QCoreApplication::translate("ResourcePanel", "This project has no resources."));
    stack_->setCurrentWidget(empty_state_);
    return;
  }
  if (!hasVisibleRow()) {
    empty_state_->setText(
        QCoreApplication::translate("ResourcePanel", "No resource matches the current filter."));
    stack_->setCurrentWidget(empty_state_);
    return;
  }
  stack_->setCurrentWidget(tree_);
}

void ResourcePanel::handleCurrentChanged(QTreeWidgetItem* current) {
  if (updating_) return;  // 程序化改动 currentItem：同步选中，不回调
  adoptUserSelection(current != nullptr ? current->data(kIdColumn, Qt::UserRole).toString()
                                        : QString());
}

void ResourcePanel::syncVisibleSelection() {
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

void ResourcePanel::adoptUserSelection(const QString& id) {
  if (id == selected_id_) return;  // 同一选中不重复回调
  selected_id_ = id;
  if (on_select_) on_select_(id);
  refreshRelations();
}

void ResourcePanel::clearSelectionByUser() {
  if (selected_id_.isEmpty() && tree_->currentItem() == nullptr) return;
  updating_ = true;
  tree_->setCurrentItem(nullptr);
  tree_->clearSelection();
  updating_ = false;
  adoptUserSelection(QString());
}

void ResourcePanel::focusTreeFromSearch() {
  if (!hasVisibleRow()) return;
  QTreeWidgetItem* target = tree_->currentItem();
  if (target == nullptr || target->isHidden()) target = firstVisibleRow();
  tree_->setFocus(Qt::ShortcutFocusReason);
  if (target != nullptr && target != tree_->currentItem()) tree_->setCurrentItem(target);
}

QTreeWidgetItem* ResourcePanel::findRow(const QString& id) const {
  if (id.isEmpty()) return nullptr;
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    QTreeWidgetItem* row = tree_->topLevelItem(index);
    if (row->data(kIdColumn, Qt::UserRole).toString() == id) return row;
  }
  return nullptr;
}

void ResourcePanel::refreshRelations() {
  if (selected_id_.isEmpty()) {
    relations_->hide();
    return;
  }
  relations_->show();
  showRelation(references_value_, referencesOf(selected_id_));
  showRelation(backlinks_value_, referencedBy(selected_id_));
}

void ResourcePanel::showRelation(QLabel* value, const QStringList& ids) {
  // 按稳定 ID 展示；引用对象不在本项目里（脏数据）也照原样显示，不静默丢弃。
  value->setText(ids.isEmpty() ? QCoreApplication::translate("ResourcePanel", "None")
                               : ids.join(QStringLiteral(", ")));
  value->setToolTip(value->text());
}

QTreeWidgetItem* ResourcePanel::firstVisibleRow() const {
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    QTreeWidgetItem* row = tree_->topLevelItem(index);
    if (!row->isHidden()) return row;
  }
  return nullptr;
}

bool ResourcePanel::hasVisibleRow() const { return firstVisibleRow() != nullptr; }

}  // namespace wds::studio
