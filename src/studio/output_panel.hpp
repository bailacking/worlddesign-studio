#pragma once

#include <QJsonArray>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace wds::studio {

// 输出面板：按入参顺序显示调用方已本地化的记录文本，并标注稳定级别 info / warning / error。
//
// 数据约定（见 docs/project/parallel-delivery.md 的 WKB-07A 任务卡）：
//   - setEntries 每项读取已本地化的 text 与稳定级别 level；
//   - 按入参顺序显示，不排序、不合并、不改写文本；
//   - 支持按级别筛选与复制选中行，筛选只改可见性，不动数据；
//   - 文案本身由调用方本地化，面板自用文案走 "OutputPanel" 翻译上下文，随语言切换重建。
//
// 首批不自定义 Qt 信号，故不声明 Q_OBJECT：避免 Qt 目标强制开启 AUTOMOC，
// 语言切换仍由 changeEvent 里的 QEvent::LanguageChange 处理。
class OutputPanel final : public QWidget {
 public:
  explicit OutputPanel(QWidget* parent = nullptr);

  // 替换全部记录。空数组表示没有输出，面板显示空态。
  void setEntries(const QJsonArray& entries);

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void rebuildRows();
  void retranslate();
  void applyLevelFilter();
  void updateEmptyState();
  void updateActions();
  void copySelection();
  [[nodiscard]] QString levelLabel(const QString& level) const;
  [[nodiscard]] QTreeWidgetItem* currentRow() const;
  [[nodiscard]] bool hasVisibleRow() const;

  QComboBox* level_filter_ = nullptr;
  QPushButton* copy_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QLabel* empty_state_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QJsonArray entries_;
};

}  // namespace wds::studio
