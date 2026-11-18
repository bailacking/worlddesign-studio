#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include <QWidget>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

struct Layer {
  QString name;
  QImage pixels;
  QPoint anchor;
};

struct Appearance {
  QImage body;
  QPoint foot;
  QPoint hook;
  std::vector<Layer> outfits;
};

void require(bool valid) {
  if (!valid) throw std::runtime_error("Invalid W15 appearance or output");
}

QImage sprite(const QDir& folder, const QJsonValue& value) {
  const auto name = value.toString();
  require(!name.isEmpty() && name == QFileInfo(name).fileName() && name.endsWith(".png"));
  QImageReader reader(folder.filePath(name), "png");
  const auto size = reader.size();
  require(size.isValid() && size.width() > 0 && size.height() > 0 && size.width() <= 128 &&
          size.height() <= 128);
  auto pixels = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
  require(!pixels.isNull() && pixels.hasAlphaChannel());
  return pixels;
}

QPoint point(const QImage& image, const QJsonValue& value) {
  const auto array = value.toArray();
  require(array.size() == 2);
  const QPoint position(array[0].toInt(-1), array[1].toInt(-1));
  require(image.rect().contains(position));
  return position;
}

Appearance read(const QDir& folder) {
  QFile file(folder.filePath("look.json"));
  require(file.open(QIODevice::ReadOnly) && file.size() <= 4096);
  const auto document = QJsonDocument::fromJson(file.readAll()).object();
  require(document.value("version").toInt() == 1);
  Appearance result;
  result.body = sprite(folder, document.value("body"));
  result.foot = point(result.body, document.value("foot"));
  result.hook = point(result.body, document.value("hook"));
  const auto rows = document.value("outfits").toArray();
  require(rows.size() >= 2 && rows.size() <= 8);
  for (const auto& row : rows) {
    const auto object = row.toObject();
    auto image = sprite(folder, object.value("file"));
    const auto anchor = point(image, object.value("anchor"));
    const auto label = object.value("name").toString();
    require(!label.isEmpty() && label.size() <= 32 && qAlpha(image.pixel(anchor)) == 255);
    result.outfits.push_back({label, std::move(image), anchor});
  }
  return result;
}

void ground(QPainter& painter) {
  painter.fillRect(QRect(0, 0, 540, 340), QColor(30, 42, 58));
  const QPoint origin{270, 80};
  for (int row = 0; row < 10; ++row) {
    for (int col = 0; col < 10; ++col) {
      const QPoint middle = origin + QPoint((col - row) * 24, (col + row) * 11);
      const QPolygon tile{middle + QPoint(0, -11), middle + QPoint(24, 0), middle + QPoint(0, 11),
                          middle + QPoint(-24, 0)};
      painter.setPen(QColor(65, 103, 101));
      painter.setBrush((row + col) % 2 ? QColor(66, 111, 103) : QColor(71, 120, 108));
      painter.drawPolygon(tile);
    }
  }
}

void barrier(QPainter& painter) {
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(33, 53, 66));
  painter.drawPolygon(
      QPolygon{QPoint(168, 183), QPoint(349, 214), QPoint(349, 236), QPoint(168, 205)});
  painter.setBrush(QColor(139, 112, 77));
  painter.drawPolygon(
      QPolygon{QPoint(168, 166), QPoint(349, 199), QPoint(349, 215), QPoint(168, 182)});
  for (int x : {168, 247, 343}) {
    const int y = 159 + (x - 168) * 33 / 181;
    painter.fillRect(x, y, 8, 49, QColor(185, 147, 94));
  }
}

QImage render_frame(const Appearance& appearance, std::size_t selected, bool behind) {
  QImage frame(540, 340, QImage::Format_RGB32);
  QPainter painter(&frame);
  ground(painter);
  const QPoint foot = behind ? QPoint(270, 181) : QPoint(270, 259);
  constexpr int scale = 3;
  const auto body_origin = foot - appearance.foot * scale;
  const auto draw = [&](QPoint origin, const QImage& image) {
    painter.drawImage(QRect(origin, image.size() * scale), image);
  };
  if (!behind) barrier(painter);
  draw(body_origin, appearance.body);
  const auto& layer = appearance.outfits[selected];
  draw(body_origin + (appearance.hook - layer.anchor) * scale, layer.pixels);
  if (behind) barrier(painter);
  painter.end();
  return frame;
}

class View final : public QWidget {
 public:
  View(const Appearance& appearance, QWidget* parent) : QWidget(parent), appearance_(appearance) {
    setMinimumSize(540, 340);
  }
  void appearance(std::size_t selected, bool behind) {
    selected_ = selected;
    behind_ = behind;
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    const auto frame = render_frame(appearance_, selected_, behind_);
    painter.fillRect(rect(), QColor(30, 42, 58));
    const auto size = frame.size().scaled(this->size(), Qt::KeepAspectRatio);
    painter.drawImage(
        QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size), frame);
  }

 private:
  const Appearance& appearance_;
  std::size_t selected_ = 0;
  bool behind_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  const auto args = app.arguments();
  try {
    if (args.size() == 4 && args[1] == "--compare") {
      const auto appearance = read(QDir(args[2]));
      const QDir output(args[3]);
      require(QDir().mkpath(output.absolutePath()));
      const auto lantern = render_frame(appearance, 0, false);
      const auto satchel = render_frame(appearance, 1, false);
      const auto behind = render_frame(appearance, 1, true);
      require(lantern != satchel && satchel != behind);
      // Sample a body pixel outside both equipment layers. It stays in place after swapping.
      const QPoint body_sample = QPoint(270, 259) + (QPoint(22, 12) - appearance.foot) * 3;
      require(lantern.pixel(body_sample) == satchel.pixel(body_sample));
      const QPoint hook = QPoint(270, 259) + (appearance.hook - appearance.foot) * 3;
      require(
          lantern.pixel(hook) == appearance.outfits[0].pixels.pixel(appearance.outfits[0].anchor) &&
          satchel.pixel(hook) == appearance.outfits[1].pixels.pixel(appearance.outfits[1].anchor));
      require(lantern.save(output.filePath("lantern.png")) &&
              satchel.save(output.filePath("satchel.png")) &&
              behind.save(output.filePath("occluded.png")));
      return 0;
    }
    if (args.size() > 2) {
      std::cerr << "Usage: wds_w15_layers [ASSETS] | --compare ASSETS OUTPUT\n";
      return 2;
    }
    const auto directory = args.size() == 2
                               ? args[1]
                               : QDir(QCoreApplication::applicationDirPath()).filePath("assets");
    const auto appearance = read(QDir(directory));
    QWidget window;
    window.setWindowTitle(QStringLiteral("WDS · 分层换装预览"));
    auto* layout = new QVBoxLayout(&window);
    auto* view = new View(appearance, &window);
    layout->addWidget(view, 1);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* outfit = new QComboBox(&window);
    for (const auto& layer : appearance.outfits) outfit->addItem(layer.name);
    auto* depth = new QComboBox(&window);
    depth->addItems({QStringLiteral("栏杆前"), QStringLiteral("栏杆后")});
    form->addRow(QStringLiteral("装备"), outfit);
    form->addRow(QStringLiteral("位置"), depth);
    const auto refresh = [&] {
      view->appearance(static_cast<std::size_t>(outfit->currentIndex()),
                       depth->currentIndex() == 1);
    };
    QObject::connect(outfit, &QComboBox::currentIndexChanged, &window, refresh);
    QObject::connect(depth, &QComboBox::currentIndexChanged, &window, refresh);
    window.resize(580, 430);
    window.show();
    return app.exec();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
