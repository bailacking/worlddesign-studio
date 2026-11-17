#include "asset_import.hpp"

#include <lodepng.h>

#include <algorithm>
#include <cstdint>

namespace wds::assets {
namespace {

constexpr unsigned max_dimension = 2048;
constexpr std::size_t max_excerpt_bytes = 4096;

const unsigned char* png_bytes(std::string_view bytes) {
  return reinterpret_cast<const unsigned char*>(bytes.data());
}

PngInfo dimensions(std::string_view bytes, lodepng::State& decoder) {
  PngInfo info{"ok"};
  if (lodepng_inspect(&info.width, &info.height, &decoder, png_bytes(bytes), bytes.size()) != 0)
    return {"invalid_png"};
  if (info.width > max_dimension || info.height > max_dimension) return {"image_too_large"};
  return info;
}

std::size_t utf8_length(std::string_view bytes) {
  const auto first = static_cast<unsigned char>(bytes.front());
  if (first < 0x80)
    return (first >= 0x20 || first == '\t' || first == '\n' || first == '\r') && first != 0x7f ? 1
                                                                                               : 0;
  if (first < 0xc2 || first > 0xf4) return 0;
  const std::size_t length = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
  if (bytes.size() < length) return 0;
  auto value = static_cast<std::uint32_t>(first & (0x7f >> length));
  for (std::size_t index = 1; index < length; ++index) {
    const auto next = static_cast<unsigned char>(bytes[index]);
    if ((next & 0xc0) != 0x80) return 0;
    value = (value << 6) | (next & 0x3f);
  }
  constexpr std::uint32_t minimum[]{0, 0, 0x80, 0x800, 0x10000};
  if (value < minimum[length] || value > 0x10ffff || (0xd800 <= value && value <= 0xdfff)) return 0;
  return length;
}

}  // namespace

PngInfo inspect_png(std::string_view bytes) {
  lodepng::State decoder;
  return dimensions(bytes, decoder);
}

PngImage decode_png(std::string_view bytes) {
  lodepng::State decoder;
  PngImage image{dimensions(bytes, decoder)};
  if (image.info.code != "ok") return image;
  // A 16-bit RGBA frame uses at most eight bytes per pixel before filtering.
  // Allow row filters and the extra scanlines of Adam7 interlacing.
  decoder.decoder.zlibsettings.max_output_size =
      (static_cast<std::size_t>(image.info.width) * 8 + 8) * (image.info.height + 8);
  if (lodepng::decode(image.rgba, image.info.width, image.info.height, decoder, png_bytes(bytes),
                      bytes.size()) != 0)
    return {{"invalid_png"}};
  return image;
}

TextPreview inspect_text(std::string_view bytes) {
  if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
  TextPreview result{"ok"};
  result.excerpt.reserve(std::min(bytes.size(), max_excerpt_bytes));
  bool excerpt_complete = false;
  bool ends_in_break = false;
  while (!bytes.empty()) {
    const auto length = utf8_length(bytes);
    if (length == 0) return {"invalid_utf8"};
    const char first = bytes.front();
    ends_in_break = first == '\r' || first == '\n';
    if (ends_in_break) ++result.lines;
    if (!excerpt_complete && result.excerpt.size() + length <= max_excerpt_bytes) {
      if (ends_in_break)
        result.excerpt += '\n';
      else
        result.excerpt.append(bytes.substr(0, length));
    } else {
      excerpt_complete = true;
    }
    bytes.remove_prefix(length);
    if (first == '\r' && bytes.starts_with('\n')) bytes.remove_prefix(1);
  }
  if (!ends_in_break && !result.excerpt.empty()) ++result.lines;
  return result;
}

}  // namespace wds::assets
