#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <functional>

#include "voxel_world.hpp"

namespace wds::platform {
struct VoxelScene;
}

namespace wds::studio {

class VoxelViewport final : public QWidget {
 public:
  explicit VoxelViewport(std::function<void()> changed, QWidget* parent = nullptr);
  void setWorld(platform::VoxelScene* scene, bool reset_camera = true);
  void clearInput();

 protected:
  void changeEvent(QEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  void tick();
  void invalidateFrame();

  std::function<void()> changed_;
  platform::VoxelScene* scene_ = nullptr;
  voxel::World* world_ = nullptr;
  voxel::Player player_;
  QImage frame_;
  QSize rendered_size_;
  QSet<int> held_;
  bool jump_ = false;
  QElapsedTimer elapsed_;
  QTimer timer_;
};

}  // namespace wds::studio
