#pragma once

#include <QJsonArray>
#include <QPointF>
#include <QWidget>
#include <functional>

namespace wds::studio {

class Viewport final : public QWidget {
 public:
  explicit Viewport(std::function<void(QString)> select, QWidget* parent = nullptr);
  void setScene(QJsonArray entities, bool running);
  void setSelectedId(QString id);
  void resetView();

 protected:
  void changeEvent(QEvent* event) override;
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  QPointF screenPosition(const QJsonObject& entity) const;

  QJsonArray entities_;
  bool running_ = false;
  QString selected_;
  std::function<void(QString)> select_;
  // 编辑态视图：只影响显示，不改实体数据。缩放以屏幕像素/世界单位计，
  // 平移是相对窗口中心的屏幕像素偏移，两者合成一个正变换供绘制与点选共用。
  double scale_ = 0.68;
  QPointF offset_;
  bool panning_ = false;
  QPointF pan_origin_;
  QPointF pan_offset_;
};

}  // namespace wds::studio
