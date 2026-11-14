#include "viewport.hpp"

#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <utility>

namespace wds::studio {

Viewport::Viewport(std::function<void(QString)> select, QWidget* parent)
    : QWidget(parent), select_(std::move(select)) {
  setMinimumSize(320, 240);
  setAccessibleName(tr("Scene viewport"));
}

void Viewport::setScene(QJsonArray entities, bool running) {
  entities_ = std::move(entities);
  running_ = running;
  update();
}

void Viewport::setSelectedId(QString id) {
  selected_ = std::move(id);
  update();
}

QPointF Viewport::screenPosition(const QJsonObject& entity) const {
  constexpr double zoom = 0.68;
  return QPointF(width() * 0.5 + zoom * entity.value("x").toDouble(),
                 height() * 0.5 - zoom * entity.value("y").toDouble());
}

void Viewport::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#16191f"));
  const auto origin = screenPosition({});
  constexpr int grid = 24;
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
  painter.drawText(QPointF(16, 25), running_ ? tr("Play mode") : tr("2D · Scene"));
  if (entities_.isEmpty()) {
    painter.setPen(QColor("#a9adb5"));
    painter.drawText(rect(), Qt::AlignCenter, tr("No entities to display"));
  }
}

void Viewport::mousePressEvent(QMouseEvent* event) {
  if (running_ || event->button() != Qt::LeftButton) return;
  for (auto it = entities_.constEnd(); it != entities_.constBegin();) {
    const auto entity = (*--it).toObject();
    const auto delta = screenPosition(entity) - event->position();
    if (delta.x() * delta.x() + delta.y() * delta.y() <= 100.0) {
      select_(entity.value("id").toString());
      return;
    }
  }
  select_({});
}

}  // namespace wds::studio
