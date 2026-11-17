#include "asset_preview.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include "asset_import.hpp"

namespace wds::studio {
namespace {

constexpr int thumbnail_limit = 128;

QString cache_path() {
  const auto location = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return location.isEmpty() ? QString{} : QDir(location).filePath("thumbnails-png-v1");
}

QSize thumbnail_size(unsigned width, unsigned height) {
  const QSize full(static_cast<int>(width), static_cast<int>(height));
  return full.width() <= thumbnail_limit && full.height() <= thumbnail_limit
             ? full
             : full.scaled(thumbnail_limit, thumbnail_limit, Qt::KeepAspectRatio)
                   .expandedTo(QSize(1, 1));
}

QImage read_cached_thumbnail(const QString& file, QSize expected) {
  const QFileInfo info(file);
  if (!info.isFile() || info.size() > 128 * 1024) return {};
  QImageReader reader(file, "png");
  return reader.size() == expected ? reader.read() : QImage{};
}

AssetPreview preview_png(std::string_view bytes) {
  const auto size = assets::inspect_png(bytes);
  if (size.code != "ok") return {size.code};
  const auto expected = thumbnail_size(size.width, size.height);
  const auto folder = cache_path();
  const auto hash =
      QCryptographicHash::hash(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())),
                               QCryptographicHash::Sha256);
  const auto file = folder.isEmpty()
                        ? QString{}
                        : QDir(folder).filePath(QString::fromLatin1(hash.toHex()) + ".png");
  if (!file.isEmpty()) {
    auto cached = read_cached_thumbnail(file, expected);
    if (!cached.isNull()) return {"ok", std::move(cached), {}, size.width, size.height};
  }

  const auto image = assets::decode_png(bytes);
  if (image.info.code != "ok") return {image.info.code};
  const QImage source(image.rgba.data(), static_cast<int>(size.width),
                      static_cast<int>(size.height), QImage::Format_RGBA8888);
  auto thumbnail = source.scaled(expected, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).copy();
  if (!file.isEmpty() && QDir().mkpath(folder)) {
    QSaveFile derived(file);
    if (derived.open(QIODevice::WriteOnly) && thumbnail.save(&derived, "PNG"))
      (void)derived.commit();
  }
  return {"ok", std::move(thumbnail), {}, size.width, size.height};
}

}  // namespace

AssetPreview preview_asset(std::string_view kind, std::string_view bytes) {
  if (kind == "image") return preview_png(bytes);
  if (kind != "text") return {"invalid_resource"};
  const auto preview = assets::inspect_text(bytes);
  return {preview.code, {}, QString::fromStdString(preview.excerpt), 0, 0, preview.lines};
}

bool clear_thumbnail_cache() {
  const auto path = cache_path();
  if (path.isEmpty()) return true;
  QDir folder(path);
  if (!folder.exists()) return true;
  if (QFileInfo(path).isSymLink()) return false;
  const QRegularExpression cache_filename("^[a-f0-9]{64}\\.png$");
  bool removed = true;
  for (const auto& file : folder.entryList({"*.png"}, QDir::Files)) {
    if (cache_filename.match(file).hasMatch()) removed = folder.remove(file) && removed;
  }
  return removed;
}

}  // namespace wds::studio
