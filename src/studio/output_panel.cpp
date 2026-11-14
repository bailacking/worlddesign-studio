#include "output_panel.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace wds::studio {
namespace {

constexpr int kLevelColumn = 0;
constexpr int kTextColumn = 1;

// 级别只有三个稳定值；未知或缺失按 info 处理，保证筛选项始终可用。
QString normalizedLevel(const QString& level) {
  if (level == QLatin1String("warning")) return QStringLiteral("warning");
  if (level == QLatin1String("error")) return QStringLiteral("error");
  return QStringLiteral("info");
}

// 与 workbench.qss 的状态色一致：info 次要文本、warning 修改中、error 失败。
QColor levelColor(const QString& level) {
  if (level == QLatin1String("warning")) return QColor(QStringLiteral("#d9a13b"));
  if (level == QLatin1String("error")) return QColor(QStringLiteral("#ef6b62"));
  return QColor(QStringLiteral("#a9adb5"));
}

}  // namespace

OutputPanel::OutputPanel(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);

  auto* controls = new QHBoxLayout();
  controls->setSpacing(4);
  level_filter_ = new QComboBox(this);
  controls->addWidget(level_filter_);
  controls->addStretch(1);
  copy_ = new QPushButton(this);
  copy_->setEnabled(false);
  controls->addWidget(copy_);
  layout->addLayout(controls);

  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(2);
  tree_->setRootIsDecorated(false);
  tree_->setUniformRowHeights(true);
  tree_->setAllColumnsShowFocus(true);
  tree_->setWordWrap(false);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  tree_->setTextElideMode(Qt::ElideRight);
  tree_->setFocusPolicy(Qt::StrongFocus);
  // 级别列按内容定宽，消息列吃掉剩余宽度；长文本悬停给全文。
  tree_->header()->setSectionResizeMode(kLevelColumn, QHeaderView::ResizeToContents);
  tree_->header()->setSectionResizeMode(kTextColumn, QHeaderView::Stretch);

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

  connect(level_filter_, &QComboBox::currentIndexChanged, this, [this] { applyLevelFilter(); });
  connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { updateActions(); });
  connect(copy_, &QPushButton::clicked, this, [this] { copySelection(); });

  auto* copy_action = new QAction(this);
  copy_action->setShortcut(QKeySequence::Copy);  // Ctrl+C / Cmd+C
  copy_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  addAction(copy_action);
  connect(copy_action, &QAction::triggered, this, [this] { copySelection(); });

  retranslate();
  updateEmptyState();
  updateActions();
}

void OutputPanel::setEntries(const QJsonArray& entries) {
  entries_ = entries;
  rebuildRows();
  applyLevelFilter();
  updateEmptyState();
  if (tree_->topLevelItemCount() > 0) tree_->scrollToBottom();
  updateActions();
}

void OutputPanel::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) retranslate();
}

void OutputPanel::rebuildRows() {
  const QSignalBlocker blocker(tree_);
  tree_->clear();
  for (const QJsonValue& value : entries_) {
    if (!value.isObject()) continue;
    const QJsonObject entry = value.toObject();
    const QString text = entry.value(QStringLiteral("text")).toString();
    if (text.isEmpty()) continue;  // 空文本没有可读信息，也不适合复制
    const QString level = normalizedLevel(entry.value(QStringLiteral("level")).toString());
    auto* row = new QTreeWidgetItem(tree_);
    row->setText(kLevelColumn, levelLabel(level));
    row->setData(kLevelColumn, Qt::UserRole, level);
    row->setForeground(kLevelColumn, levelColor(level));
    row->setText(kTextColumn, text);
    row->setToolTip(kTextColumn, text);
  }
}

void OutputPanel::retranslate() {
  tree_->setHeaderLabels({QCoreApplication::translate("OutputPanel", "Level"),
                          QCoreApplication::translate("OutputPanel", "Message")});
  tree_->setAccessibleName(QCoreApplication::translate("OutputPanel", "Output entries"));
  level_filter_->setAccessibleName(
      QCoreApplication::translate("OutputPanel", "Output level filter"));
  copy_->setText(QCoreApplication::translate("OutputPanel", "Copy"));
  copy_->setToolTip(QCoreApplication::translate("OutputPanel", "Copy the selected entry (Ctrl+C)"));

  const QString selected = level_filter_->currentData().toString();
  const QSignalBlocker blocker(level_filter_);
  level_filter_->clear();
  level_filter_->addItem(QCoreApplication::translate("OutputPanel", "All levels"), QString());
  level_filter_->addItem(QCoreApplication::translate("OutputPanel", "Info"),
                         QStringLiteral("info"));
  level_filter_->addItem(QCoreApplication::translate("OutputPanel", "Warning"),
                         QStringLiteral("warning"));
  level_filter_->addItem(QCoreApplication::translate("OutputPanel", "Error"),
                         QStringLiteral("error"));
  const int restored = level_filter_->findData(selected);
  level_filter_->setCurrentIndex(restored >= 0 ? restored : 0);

  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    auto* row = tree_->topLevelItem(index);
    row->setText(kLevelColumn, levelLabel(row->data(kLevelColumn, Qt::UserRole).toString()));
  }
}

void OutputPanel::applyLevelFilter() {
  const QString level = level_filter_->currentData().toString();
  const QSignalBlocker blocker(tree_);
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    auto* row = tree_->topLevelItem(index);
    row->setHidden(!level.isEmpty() && row->data(kLevelColumn, Qt::UserRole).toString() != level);
  }
  // 选中行被筛掉时不保留隐藏的选中，避免复制到看不见的条目。
  auto* current = currentRow();
  if (current != nullptr && current->isHidden()) tree_->setCurrentItem(nullptr);
  updateEmptyState();
  updateActions();
}

void OutputPanel::updateEmptyState() {
  if (tree_->topLevelItemCount() == 0) {
    empty_state_->setText(QCoreApplication::translate("OutputPanel", "No output yet."));
    stack_->setCurrentWidget(empty_state_);
    return;
  }
  if (!hasVisibleRow()) {
    empty_state_->setText(QCoreApplication::translate("OutputPanel", "No entries for this level."));
    stack_->setCurrentWidget(empty_state_);
    return;
  }
  stack_->setCurrentWidget(tree_);
}

void OutputPanel::updateActions() {
  auto* current = currentRow();
  copy_->setEnabled(current != nullptr && !current->isHidden());
}

void OutputPanel::copySelection() {
  auto* current = currentRow();
  if (current == nullptr || current->isHidden()) return;
  // 复制消息列原文：级别是可重建的元信息，粘进报告里只需要文本本身。
  QApplication::clipboard()->setText(current->text(kTextColumn));
}

QString OutputPanel::levelLabel(const QString& level) const {
  if (level == QLatin1String("warning"))
    return QCoreApplication::translate("OutputPanel", "Warning");
  if (level == QLatin1String("error")) return QCoreApplication::translate("OutputPanel", "Error");
  return QCoreApplication::translate("OutputPanel", "Info");
}

QTreeWidgetItem* OutputPanel::currentRow() const { return tree_->currentItem(); }

bool OutputPanel::hasVisibleRow() const {
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    if (!tree_->topLevelItem(index)->isHidden()) return true;
  }
  return false;
}

}  // namespace wds::studio
