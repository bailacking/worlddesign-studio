#include "viewport.hpp"

#include <QCoreApplication>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

namespace wds::studio {
namespace {

// 缩放范围：0.15 视野约 6.7 倍于默认，8.0 用于贴近查看单个实体。
constexpr double kMinScale = 0.15;
constexpr double kMaxScale = 8.0;
constexpr double kDefaultScale = 0.68;
constexpr double kWheelStep = 1.2;  // 每 120 单位滚轮一格放大 20%
constexpr double kPickRadius = 10.0;

}  // namespace

Viewport::Viewport(std::function<void(QString)> select, QWidget* parent)
    : QWidget(parent), select_(std::move(select)) {
  setMinimumSize(320, 240);
  setAccessibleName(QCoreApplication::translate("Viewport", "Scene viewport"));
}

void Viewport::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    setAccessibleName(QCoreApplication::translate("Viewport", "Scene viewport"));
    update();
  }
}

void Viewport::setScene(QJsonArray entities, bool running) {
  entities_ = std::move(entities);
  running_ = running;
  // 试玩期间快捷操作让给运行态，退出后仍以退出前的编辑视图呈现。
  if (running_) {
    panning_ = false;
    unsetCursor();
  }
  update();
}

void Viewport::setSelectedId(QString id) {
  selected_ = std::move(id);
  update();
}

void Viewport::resetView() {
  panning_ = false;
  unsetCursor();
  scale_ = kDefaultScale;
  offset_ = {};
  update();
}

QPointF Viewport::screenPosition(const QJsonObject& entity) const {
  return QPointF(width() * 0.5 + offset_.x() + scale_ * entity.value("x").toDouble(),
                 height() * 0.5 + offset_.y() - scale_ * entity.value("y").toDouble());
}

void Viewport::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#16191f"));
  const auto origin = screenPosition({});
  const int grid = std::max(4, static_cast<int>(std::lround(24.0 * scale_ / kDefaultScale)));
  painter.setPen(QColor("#242933"));
  for (int x = static_cast<int>(origin.x()) % grid; x < width(); x += grid)
    painter.drawLine(x, 0, x, height());
  for (int y = static_cast<int>(origin.y()) % grid; y < height(); y += grid)
    painter.drawLine(0, y, width(), y);
  painter.setPen(QColor("#454c5b"));
  painter.drawLine(QPointF(origin.x(), 0), QPointF(origin.x(), height()));
  painter.drawLine(QPointF(0, origin.y()), QPointF(width(), origin.y()));

  painter.setRenderHint(QPainter::Antialiasing);
  for (const auto value : entities_) {
    const auto entity = value.toObject();
    const auto name = entity.value("id").toString();
    const auto position = screenPosition(entity);
    const bool selected = name == selected_;
    painter.setPen(QPen(selected ? QColor("#93caff") : QColor("#d5dfed"), selected ? 2 : 1));
    painter.setBrush(selected ? QColor("#438ff2") : QColor("#526a8a"));
    painter.drawEllipse(position, 6, 6);
    painter.setPen(QColor("#d5dfed"));
    painter.drawText(position + QPointF(12, -8), name);
  }
  painter.setPen(running_ ? QColor("#57c08a") : QColor("#a9adb5"));
  painter.drawText(QPointF(16, 25), running_
                                        ? QCoreApplication::translate("Viewport", "Play mode")
                                        : QCoreApplication::translate("Viewport", "2D · Scene"));
  if (entities_.isEmpty()) {
    painter.setPen(QColor("#a9adb5"));
    painter.drawText(rect(), Qt::AlignCenter,
                     QCoreApplication::translate("Viewport", "No entities to display"));
  }
}

void Viewport::mousePressEvent(QMouseEvent* event) {
  if (running_) return;  // 运行态独占输入，退出试玩才恢复编辑操作
  if (event->button() == Qt::MiddleButton) {
    panning_ = true;
    pan_origin_ = event->position();
    pan_offset_ = offset_;
    setCursor(Qt::ClosedHandCursor);
    return;
  }
  if (event->button() != Qt::LeftButton) return;
  // 命中测试与绘制共用 screenPosition，缩放平移后点选位置不会漂。
  for (auto it = entities_.constEnd(); it != entities_.constBegin();) {
    const auto entity = (*--it).toObject();
    const auto delta = screenPosition(entity) - event->position();
    if (delta.x() * delta.x() + delta.y() * delta.y() <= kPickRadius * kPickRadius) {
      select_(entity.value("id").toString());
      return;
    }
  }
  select_({});
}

void Viewport::mouseMoveEvent(QMouseEvent* event) {
  if (!panning_) return;
  offset_ = pan_offset_ + (event->position() - pan_origin_);
  update();
}

void Viewport::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::MiddleButton || !panning_) return;
  panning_ = false;
  unsetCursor();
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* event) {
  if (running_ || event->button() != Qt::MiddleButton) return;
  resetView();
}

void Viewport::wheelEvent(QWheelEvent* event) {
  if (running_) {
    QWidget::wheelEvent(event);  // 试玩时滚轮交还运行态
    return;
  }
  const int delta = event->angleDelta().y();
  if (delta == 0) return;
  const double factor = std::pow(kWheelStep, delta / 120.0);
  const double next = std::clamp(scale_ * factor, kMinScale, kMaxScale);
  if (next == scale_) {
    event->accept();
    return;
  }
  // 以鼠标为锚：光标下的世界坐标在缩放前后保持不动。
  const QPointF center(width() * 0.5, height() * 0.5);
  const QPointF anchor = event->position();
  const QPointF world = (anchor - center - offset_) / scale_;
  scale_ = next;
  offset_ = anchor - center - world * scale_;
  event->accept();
  update();
}

}  // namespace wds::studio
