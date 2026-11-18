#pragma once

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QPointF>
#include <QWidget>
#include <functional>

#include "scene_play.hpp"

namespace wds::studio {

class Viewport final : public QWidget {
 public:
  explicit Viewport(std::function<void(QString)> select, QWidget* parent = nullptr);
  void setScene(QJsonArray entities, bool running);
  void beginPlayback(const platform::SceneSession& session);
  const platform::SceneInput& sceneInput() const { return input_; }
  void clearSceneInput() { input_.reset(); }
  void setSelectedId(QString id);
  void resetView();

 protected:
  void changeEvent(QEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  QPointF screenPosition(double x, double y) const;
  void drawTiles(QPainter& painter, bool canopy) const;
  double viewScale() const;
  bool setSceneKey(QKeyEvent* event, bool down);

  QJsonArray entities_;
  bool running_ = false;
  bool interactive_ = false;
  const platform::SceneSession* playback_ = nullptr;
  platform::SceneInput input_;
  QHash<QString, QImage> sprites_;
  QRectF play_bounds_;
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
