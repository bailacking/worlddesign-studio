#include "voxel_viewport.hpp"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "platform_project.hpp"
#include "theme/theme.hpp"

namespace wds::studio {
namespace {

bool movementKey(int key) {
  switch (key) {
    case Qt::Key_W:
    case Qt::Key_A:
    case Qt::Key_S:
    case Qt::Key_D:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Space:
      return true;
    default:
      return false;
  }
}

}  // namespace

VoxelViewport::VoxelViewport(std::function<void()> changed, QWidget* parent)
    : QWidget(parent), changed_(std::move(changed)) {
  setObjectName("voxelViewport");
  setAccessibleName(QCoreApplication::translate("VoxelViewport", "3D voxel viewport"));
  setMinimumSize(320, 240);
  setFocusPolicy(Qt::ClickFocus);
  timer_.setInterval(16);
  timer_.setTimerType(Qt::PreciseTimer);
  connect(&timer_, &QTimer::timeout, this, &VoxelViewport::tick);
}

void VoxelViewport::setWorld(platform::VoxelScene* scene, bool reset_camera) {
  scene_ = scene;
  world_ = scene ? &scene->world : nullptr;
  if (reset_camera) player_ = {};
  if (world_) {
    world_->stream(player_.feet);
    if (world_->collides(player_.feet)) {
      player_ = {};
      world_->stream(player_.feet);
    }
  }
  clearInput();
  if (world_) {
    elapsed_.start();
    timer_.start();
  } else {
    timer_.stop();
  }
  invalidateFrame();
}

void VoxelViewport::clearInput() {
  held_.clear();
  jump_ = false;
}

void VoxelViewport::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    setAccessibleName(QCoreApplication::translate("VoxelViewport", "3D voxel viewport"));
    update();
  }
}

void VoxelViewport::focusOutEvent(QFocusEvent* event) {
  clearInput();
  QWidget::focusOutEvent(event);
}

void VoxelViewport::keyPressEvent(QKeyEvent* event) {
  if (!movementKey(event->key()) ||
      (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
    QWidget::keyPressEvent(event);
    return;
  }
  if (!event->isAutoRepeat()) {
    if (event->key() == Qt::Key_Space && !held_.contains(Qt::Key_Space)) jump_ = true;
    held_.insert(event->key());
  }
  event->accept();
}

void VoxelViewport::keyReleaseEvent(QKeyEvent* event) {
  if (!movementKey(event->key())) {
    QWidget::keyReleaseEvent(event);
    return;
  }
  if (!event->isAutoRepeat()) held_.remove(event->key());
  event->accept();
}

void VoxelViewport::mousePressEvent(QMouseEvent* event) {
  if (!world_ || (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)) {
    QWidget::mousePressEvent(event);
    return;
  }
  setFocus(Qt::MouseFocusReason);
  if (scene_->edit(player_.feet, player_.eye(), player_.view(),
                   event->button() == Qt::RightButton)) {
    invalidateFrame();
    changed_();
  }
  event->accept();
}

void VoxelViewport::tick() {
  const auto seconds = std::clamp(static_cast<double>(elapsed_.restart()) / 1000, 0.0, 0.025);
  if (!world_ || !isEnabled() || !isVisible() || !hasFocus() || !window()->isActiveWindow()) return;
  if (held_.isEmpty() && player_.grounded && !jump_) return;
  const auto axis = [this](int positive, int negative) {
    return static_cast<int>(held_.contains(positive)) - static_cast<int>(held_.contains(negative));
  };
  const auto before = player_;
  player_.yaw = std::remainder(player_.yaw + axis(Qt::Key_Right, Qt::Key_Left) * seconds * 1.8,
                               2 * std::numbers::pi);
  player_.pitch =
      std::clamp(player_.pitch + axis(Qt::Key_Up, Qt::Key_Down) * seconds * 1.4, -1.3, 1.3);
  player_.step(*world_, axis(Qt::Key_W, Qt::Key_S), axis(Qt::Key_D, Qt::Key_A),
               std::exchange(jump_, false), seconds);
  if (before.yaw != player_.yaw || before.pitch != player_.pitch ||
      before.feet.x != player_.feet.x || before.feet.y != player_.feet.y ||
      before.feet.z != player_.feet.z)
    invalidateFrame();
}

void VoxelViewport::invalidateFrame() {
  frame_ = {};
  update();
}

void VoxelViewport::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  if (!world_ || width() <= 0 || height() <= 0) {
    painter.fillRect(rect(), theme::color::viewport_background);
    return;
  }
  const auto camera = player_.camera();
  if (frame_.isNull() || rendered_size_ != size()) {
    rendered_size_ = size();
    constexpr int samples = 240;
    const auto rows = std::clamp(
        static_cast<int>(std::lround(samples * static_cast<double>(height()) / width())), 90, 200);
    frame_ = QImage(samples, rows, QImage::Format_RGB32);
    const double aspect = static_cast<double>(width()) / height();
    for (int y = 0; y < rows; ++y) {
      auto* row = reinterpret_cast<QRgb*>(frame_.scanLine(y));
      const double v = (1 - 2 * (y + 0.5) / rows) * 0.58;
      for (int x = 0; x < samples; ++x) {
        const double h = (2 * (x + 0.5) / samples - 1) * 0.58 * aspect;
        row[x] = 0xff000000U | voxel::pixel(*world_, camera.eye, camera.ray(h, v));
      }
    }
  }
  painter.drawImage(rect(), frame_);
  const auto hit = world_->cast(camera.eye, camera.forward, 7);
  const bool target = hit.block != voxel::Block::air;
  const bool breakable = target && hit.y > 0 && world_->content().style(hit.block).breakable;
  const auto center = rect().center();
  painter.setPen(QPen(theme::color::viewport_background, 4));
  painter.drawLine(center - QPoint(10, 0), center + QPoint(10, 0));
  painter.drawLine(center - QPoint(0, 10), center + QPoint(0, 10));
  painter.setPen(QPen(breakable ? theme::color::warning_text : theme::color::selected_text, 2));
  painter.drawLine(center - QPoint(8, 0), center + QPoint(8, 0));
  painter.drawLine(center - QPoint(0, 8), center + QPoint(0, 8));

  const auto selected = target ? QCoreApplication::translate("VoxelViewport", "Block (%1, %2, %3)")
                                     .arg(hit.x)
                                     .arg(hit.y)
                                     .arg(hit.z)
                               : QCoreApplication::translate("VoxelViewport", "No block in reach");
  const auto bounds = rect().adjusted(12, 12, -12, -12);
  const auto hint = QCoreApplication::translate(
      "VoxelViewport", "WASD move · Arrows look · Space jump · Left break · Right place");
  painter.setPen(theme::color::text);
  const auto drawLabel = [&](const QString& text, int alignment) {
    const auto area = painter.boundingRect(bounds, alignment | Qt::TextWordWrap, text);
    painter.fillRect(area.adjusted(-6, -3, 6, 3), theme::color::viewport_background);
    painter.drawText(area, Qt::TextWordWrap, text);
  };
  drawLabel(selected, Qt::AlignTop | Qt::AlignLeft);
  drawLabel(hint, Qt::AlignBottom | Qt::AlignLeft);
}

}  // namespace wds::studio
