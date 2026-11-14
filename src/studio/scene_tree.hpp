#pragma once

#include <QJsonArray>
#include <QString>
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace wds::studio {

// 场景树面板：按输入顺序列出入口场景的实体稳定 ID，提供大小写不敏感的搜索与键盘选择。
//
// 数据约定（见 docs/architecture/workbench.md「首批可并行交付的组件」）：
//   - 调用方传入的 JSON 已由工作台桥接层确认基本结构，本面板只读取每条实体的 id；
//   - 面板自己保存显示数据，项目切换时传空数组清空；
//   - 用户主动选择时回调一次当前稳定 ID，清空选择回传空字符串；
//   - 更新模型（setEntities）与外部同步选中（setSelectedId）不触发回调；
//   - 过滤时保留逻辑选中 ID，清除过滤后恢复可见选中。
//
// 首批不自定义 Qt 信号，故不声明 Q_OBJECT：调用方只需一个 std::function 回调，
// 也避免 Qt 目标强制开启 AUTOMOC。文案使用独立的 SceneTreePanel 翻译上下文。
class SceneTreePanel final : public QWidget {
 public:
  explicit SceneTreePanel(std::function<void(QString)> onSelect, QWidget* parent = nullptr);

  // 重建实体列表。保留仍存在的逻辑选中；传入空数组视为关闭项目，同时清空选中。
  void setEntities(const QJsonArray& entities);

  // 由其他面板联动选中。不触发回调；目标被搜索过滤隐藏时只保留逻辑选中。
  void setSelectedId(const QString& id);

  // 当前逻辑选中 ID，无选中时为空串。回调之后与外部状态一致，供集成方回读。
  [[nodiscard]] QString selectedId() const;

 protected:
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void retranslate();
  void applyFilter();
  void updateEmptyState();
  void handleCurrentChanged(QTreeWidgetItem* current);
  void syncVisibleSelection();
  void adoptUserSelection(const QString& id);
  void clearSelectionByUser();
  void focusTreeFromSearch();
  [[nodiscard]] QTreeWidgetItem* findRow(const QString& id) const;
  [[nodiscard]] QTreeWidgetItem* firstVisibleRow() const;
  [[nodiscard]] bool hasVisibleRow() const;

  std::function<void(QString)> on_select_;
  QLineEdit* search_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QLabel* empty_state_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QString selected_id_;
  // 程序化改动 currentItem 时抑制回调：Qt 无法区分用户与代码触发的选中变化。
  bool updating_ = false;
};

}  // namespace wds::studio
