#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace wds::studio {

// 资源面板：列出项目资源的稳定 ID、类型与路径，按 ID 或路径搜索，并按类型筛选。
//
// 数据约定见 docs/architecture/workbench.md 的「组件与交互规范」。
//   - 输入沿用 `project resources` 的条目，本面板只读取 id、kind、path 与 refs；
//   - 面板自己保存显示数据，项目切换时传空数组清空；
//   - 用户主动选择时回调一次当前稳定 ID，清空选择回传空字符串；
//   - 更新模型（setResources）与外部同步选中（setSelectedId）不触发回调；
//   - 过滤（搜索与类型同为过滤条件）时保留逻辑选中 ID，清除过滤后恢复可见选中。
//
// 引用查看（PRD-04）：选中项下方显示其直接引用与被引用项，按稳定 ID 列出。
// 被引用项由全量资源反查得出，不随搜索与类型过滤变化；该区域只读，不改变选择回调。
//
// 首批不自定义 Qt 信号，故不声明 Q_OBJECT：调用方只需一个 std::function 回调，
// 也避免 Qt 目标强制开启 AUTOMOC。文案统一走 QCoreApplication::translate，
// 文案使用独立的 ResourcePanel 翻译上下文。
class ResourcePanel final : public QWidget {
 public:
  explicit ResourcePanel(std::function<void(QString)> onSelect, QWidget* parent = nullptr);

  // 重建资源列表。保留仍存在的逻辑选中；传入空数组视为关闭项目，同时清空选中。
  void setResources(const QJsonArray& resources);

  // 由其他面板联动选中。不触发回调；目标被过滤隐藏时只保留逻辑选中。
  void setSelectedId(const QString& id);

  // 当前逻辑选中 ID，无选中时为空串。回调之后与外部状态一致，供集成方回读。
  [[nodiscard]] QString selectedId() const;

  // 选中项的直接引用 ID，按输入顺序去重；无选中或未声明引用时为空。
  [[nodiscard]] QStringList referencesOf(const QString& id) const;

  // 引用该资源的资源 ID，按列表顺序；用于从被引用的一端反查。
  [[nodiscard]] QStringList referencedBy(const QString& id) const;

 protected:
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void retranslate();
  void rebuildKindFilter();
  void applyFilter();
  void updateEmptyState();
  void syncVisibleSelection();
  void handleCurrentChanged(QTreeWidgetItem* current);
  void adoptUserSelection(const QString& id);
  void clearSelectionByUser();
  void focusTreeFromSearch();
  void refreshRelations();
  void showRelation(QLabel* value, const QStringList& ids);
  [[nodiscard]] QTreeWidgetItem* findRow(const QString& id) const;
  [[nodiscard]] QTreeWidgetItem* firstVisibleRow() const;
  [[nodiscard]] bool hasVisibleRow() const;

  std::function<void(QString)> on_select_;
  QLineEdit* search_ = nullptr;
  QComboBox* kind_filter_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QLabel* empty_state_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  // 引用查看区：无选中时整体隐藏，避免空面板占掉列表高度。
  QWidget* relations_ = nullptr;
  QLabel* references_caption_ = nullptr;
  QLabel* references_value_ = nullptr;
  QLabel* backlinks_caption_ = nullptr;
  QLabel* backlinks_value_ = nullptr;
  QString selected_id_;
  // 程序化改动 currentItem 时抑制回调：Qt 无法区分用户与代码触发的选中变化。
  bool updating_ = false;
};

}  // namespace wds::studio
