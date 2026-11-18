#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace wds::assets {

struct PngInfo {
  std::string_view code;
  unsigned width = 0;
  unsigned height = 0;
};

struct PngImage {
  PngInfo info;
  std::vector<unsigned char> rgba{};
};

struct TextPreview {
  std::string_view code;
  std::size_t lines = 0;
  std::string excerpt{};
};

PngInfo inspect_png(std::string_view bytes);
PngImage decode_png(std::string_view bytes);
TextPreview inspect_text(std::string_view bytes);

}  // namespace wds::assets
