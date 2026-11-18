#pragma once

#include <QImage>
#include <QString>
#include <optional>
#include <string_view>

namespace wds::studio {

struct AssetPreview {
  std::string_view code = "ok";
  QImage thumbnail{};
  QString text{};
  unsigned width = 0;
  unsigned height = 0;
  std::optional<std::size_t> lines{};
};

AssetPreview preview_asset(std::string_view kind, std::string_view bytes);
bool clear_thumbnail_cache();

}  // namespace wds::studio
