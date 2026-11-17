#include "viewport.hpp"

#include <QCoreApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

#include "theme/theme.hpp"

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
  } else {
    interactive_ = false;
    input_.reset();
    sprites_.clear();
    setFocusPolicy(Qt::NoFocus);
  }
  update();
}

void Viewport::beginPlayback(const platform::SceneSession& session) {
  for (const auto& sprite : session.sprites()) {
    const QImage image(sprite.rgba.data(), static_cast<int>(sprite.width),
                       static_cast<int>(sprite.height), QImage::Format_RGBA8888);
    sprites_.insert(QString::fromStdString(sprite.id), image.copy());
  }
  interactive_ = session.snapshot().interactive;
  if (!interactive_) return;
  const auto bounds = platform::scene_play_bounds(session.snapshot());
  play_bounds_ =
      QRectF(QPointF(static_cast<double>(bounds.left), static_cast<double>(bounds.bottom)),
             QPointF(static_cast<double>(bounds.right), static_cast<double>(bounds.top)));
  setFocusPolicy(Qt::ClickFocus);
}

void Viewport::focusOutEvent(QFocusEvent* event) {
  clearSceneInput();
  QWidget::focusOutEvent(event);
}

bool Viewport::setSceneKey(QKeyEvent* event, bool down) {
  if (!interactive_) return false;
  constexpr auto ignored = Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
  if (down && (event->modifiers() & ignored)) return false;
  const int key = event->key();
  std::optional<platform::SceneKey> mapped;
  if (Qt::Key_A <= key && key <= Qt::Key_Z) {
    mapped = static_cast<platform::SceneKey>(key - Qt::Key_A);
  } else {
    switch (key) {
      case Qt::Key_Left:
        mapped = platform::SceneKey::left;
        break;
      case Qt::Key_Right:
        mapped = platform::SceneKey::right;
        break;
      case Qt::Key_Up:
        mapped = platform::SceneKey::up;
        break;
      case Qt::Key_Down:
        mapped = platform::SceneKey::down;
        break;
      default:
        return false;
    }
  }
  if (!event->isAutoRepeat()) input_.set(static_cast<std::size_t>(*mapped), down);
  event->accept();
  return true;
}

void Viewport::keyPressEvent(QKeyEvent* event) {
  if (!setSceneKey(event, true)) QWidget::keyPressEvent(event);
}

void Viewport::keyReleaseEvent(QKeyEvent* event) {
  if (!setSceneKey(event, false)) QWidget::keyReleaseEvent(event);
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

double Viewport::viewScale() const {
  if (!interactive_) return scale_;
  return std::min(
      {2.0, width() / (play_bounds_.width() + 56.0), height() / (play_bounds_.height() + 56.0)});
}

QPointF Viewport::screenPosition(const QJsonObject& entity) const {
  const auto center = interactive_ ? play_bounds_.center() : QPointF{};
  const auto offset = interactive_ ? QPointF{} : offset_;
  return QPointF(
      width() * 0.5 + offset.x() + viewScale() * (entity.value("x").toDouble() - center.x()),
      height() * 0.5 + offset.y() - viewScale() * (entity.value("y").toDouble() - center.y()));
}

void Viewport::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), theme::color::viewport_background);
  const auto origin = screenPosition({});
  const int grid = std::max(4, static_cast<int>(std::lround(24.0 * viewScale() / kDefaultScale)));
  painter.setPen(theme::color::viewport_grid);
  for (int x = static_cast<int>(origin.x()) % grid; x < width(); x += grid)
    painter.drawLine(x, 0, x, height());
  for (int y = static_cast<int>(origin.y()) % grid; y < height(); y += grid)
    painter.drawLine(0, y, width(), y);
  painter.setPen(theme::color::viewport_axes);
  painter.drawLine(QPointF(origin.x(), 0), QPointF(origin.x(), height()));
  painter.drawLine(QPointF(0, origin.y()), QPointF(width(), origin.y()));

  painter.setRenderHint(QPainter::Antialiasing);
  for (const auto value : entities_) {
    const auto entity = value.toObject();
    const auto name = entity.value("id").toString();
    const auto position = screenPosition(entity);
    const auto half_width = entity.value("half_width").toDouble() * viewScale();
    const auto half_height = entity.value("half_height").toDouble() * viewScale();
    if (running_ && half_width > 0) {
      const QRectF area(position - QPointF(half_width, half_height),
                        position + QPointF(half_width, half_height));
      const auto image = sprites_.constFind(entity.value("sprite").toString());
      if (image != sprites_.cend()) {
        painter.drawImage(area, *image);
      } else {
        const auto role = static_cast<platform::SceneRole>(entity.value("role").toInt());
        QColor color = theme::color::entity_fill;
        if (role == platform::SceneRole::goal) color = theme::color::running;
        if (role == platform::SceneRole::hazard) color = theme::color::scene_hazard;
        painter.fillRect(area, color);
      }
      continue;
    }
    const bool selected = name == selected_;
    painter.setPen(QPen(selected ? theme::color::entity_selected_line : theme::color::entity_line,
                        selected ? 2 : 1));
    painter.setBrush(selected ? theme::color::entity_selected_fill : theme::color::entity_fill);
    painter.drawEllipse(position, 6, 6);
    painter.setPen(theme::color::entity_line);
    painter.drawText(position + QPointF(12, -8), name);
  }
  painter.setPen(running_ ? theme::color::running : theme::color::secondary_text);
  painter.drawText(QPointF(16, 25), running_
                                        ? QCoreApplication::translate("Viewport", "Play mode")
                                        : QCoreApplication::translate("Viewport", "2D · Scene"));
  if (entities_.isEmpty()) {
    painter.setPen(theme::color::secondary_text);
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
