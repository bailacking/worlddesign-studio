#include <QApplication>
#include <QDir>
#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QPainter>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include "scene.hpp"

namespace {
class Raster {
 public:
  explicit Raster(const QString& directory) {
    for (std::size_t variant = 0; variant < images_.size(); ++variant) {
      const auto folder = QDir(directory).filePath(variant == 0 ? "a" : "b");
      for (std::size_t item = 0; item < w4::asset_names.size(); ++item) {
        images_[variant][item] = QImage(QDir(folder).filePath(w4::asset_names[item]))
                                     .convertToFormat(QImage::Format_ARGB32_Premultiplied);
      }
    }
  }

  bool ready() const {
    for (const auto& variant : images_)
      for (const auto& image : variant)
        if (image.isNull()) return false;
    return true;
  }

  void render(const w4::Scene& scene, QPaintDevice& destination, bool show_help = false) {
    QPainter painter(&destination);
    painter_ = &painter;
    w4::draw(scene, *this);
    if (show_help) {
      painter.setPen(Qt::white);
      painter.drawText(18, 30,
                       QString("Reef Courier | %1   ·   Lanterns %2 / 3")
                           .arg(scene.variant == 0 ? "A" : "B")
                           .arg(scene.score()));
      painter.drawText(
          18, w4::height - 18,
          scene.won ? "Delivered!   R: restart" : "Arrows / WASD: move   Tab: A/B   R: restart");
    }
    painter_ = nullptr;
  }

  void fill(int x, int y, int width, int height, w4::Color color) {
    painter_->fillRect(x, y, width, height, QColor(color.r, color.g, color.b));
  }

  void texture(int variant, w4::Asset asset, int x, int y) {
    painter_->drawImage(x, y, images_[variant][static_cast<std::size_t>(asset)]);
  }

 private:
  std::array<std::array<QImage, 3>, 2> images_;
  QPainter* painter_ = nullptr;
};

class Game final : public QWidget {
 public:
  explicit Game(Raster& raster) : raster_(raster) {
    setFixedSize(w4::width, w4::height);
    setWindowTitle("Reef Courier · Qt raster");
    setFocusPolicy(Qt::StrongFocus);
    connect(&timer_, &QTimer::timeout, this, [this] {
      scene_.advance({.left = held(Qt::Key_Left, Qt::Key_A),
                      .right = held(Qt::Key_Right, Qt::Key_D),
                      .up = held(Qt::Key_Up, Qt::Key_W),
                      .down = held(Qt::Key_Down, Qt::Key_S)});
      update();
    });
    timer_.start(16);
  }

 protected:
  void paintEvent(QPaintEvent*) override { raster_.render(scene_, *this, true); }

  void keyPressEvent(QKeyEvent* event) override {
    if (event->isAutoRepeat()) return;
    keys_.insert(event->key());
    if (event->key() == Qt::Key_Tab) scene_.variant ^= 1;
    if (event->key() == Qt::Key_R) scene_ = w4::Scene{};
  }

  void keyReleaseEvent(QKeyEvent* event) override {
    if (!event->isAutoRepeat()) keys_.remove(event->key());
  }

  void focusOutEvent(QFocusEvent* event) override {
    keys_.clear();
    QWidget::focusOutEvent(event);
  }

  bool focusNextPrevChild(bool) override { return false; }

 private:
  bool held(int arrow, int letter) const { return keys_.contains(arrow) || keys_.contains(letter); }

  Raster& raster_;
  w4::Scene scene_;
  QSet<int> keys_;
  QTimer timer_;
};
}  // namespace

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  const auto arguments = application.arguments();
  const bool bench = arguments.size() == 4 && arguments[1] == "--bench";
  if (!bench && arguments.size() > 2) {
    std::fputs("Usage: w4_qt [ASSETS] | --bench ASSETS FRAME.png\n", stderr);
    return 2;
  }
  const auto assets = arguments.size() == 1
                          ? QDir(application.applicationDirPath()).filePath("assets")
                          : arguments[bench ? 2 : 1];
  Raster raster(assets);
  if (!raster.ready()) {
    std::fputs("Could not load both asset variants.\n", stderr);
    return 1;
  }
  if (bench) {
    QImage frame(w4::width, w4::height, QImage::Format_ARGB32_Premultiplied);
    const int result = w4::benchmark(
        "qt-raster", [&](const w4::Scene& scene) { raster.render(scene, frame); },
        [&] { return frame.copy().pixelColor(w4::width - 34, w4::height / 2).green(); });
    return result == 0 && frame.save(arguments[3]) ? 0 : 1;
  }
  Game game(raster);
  game.show();
  return application.exec();
}
