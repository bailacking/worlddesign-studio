#include "voxel_content.hpp"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace wds::voxel {
namespace {

constexpr std::array names{"air",       "grass",      "dirt", "stone",  "copper",
                           "quartz",    "timber",     "rail", "spring", "water-low",
                           "water-mid", "water-high", "flame"};
constexpr std::size_t legacy_materials = 4;
constexpr std::string_view signature = "wds-sandbox-content-v1";

std::optional<std::uint32_t> color(std::string_view text) {
  if (text.size() != 6) return std::nullopt;
  std::uint32_t value{};
  const auto end = text.data() + text.size();
  const auto parsed = std::from_chars(text.data(), end, value, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
  return value;
}

}  // namespace

const BlockStyle& Content::style(Block block) const {
  return blocks.at(static_cast<std::size_t>(block));
}

std::string_view block_name(Block block) { return names.at(static_cast<std::size_t>(block)); }

std::optional<Content> template_content(std::string_view name) {
  Content content;
  if (name == "meadow") return content;
  if (name != "mesa") return std::nullopt;
  content.sky = 0xe9af8b;
  content.placement = Block::dirt;
  content.blocks[1].color = 0xc89351;
  content.blocks[2].color = 0xb66a43;
  content.blocks[3] = {0x774a55, false};
  return content;
}

std::optional<Content> read_content(std::istream& input) {
  std::string line;
  if (!std::getline(input, line)) return std::nullopt;
  if (line.ends_with('\r')) line.pop_back();
  if (line != signature) return std::nullopt;

  Content result;
  bool seen_sky = false;
  bool seen_placement = false;
  std::array<bool, legacy_materials> seen_block{};
  while (std::getline(input, line)) {
    std::istringstream row(line);
    std::string key, value, flag, extra;
    if (!(row >> key)) continue;
    if (!(row >> value)) return std::nullopt;
    if (key == "sky" && !seen_sky && !(row >> extra)) {
      const auto parsed = color(value);
      if (!parsed) return std::nullopt;
      result.sky = *parsed;
      seen_sky = true;
    } else if (key == "place" && !seen_placement && !(row >> extra)) {
      for (std::size_t i = 1; i < legacy_materials; ++i) {
        if (value == names[i]) {
          result.placement = static_cast<Block>(i);
          seen_placement = true;
        }
      }
      if (!seen_placement) return std::nullopt;
    } else {
      std::size_t i = 1;
      while (i < legacy_materials && key != names[i]) ++i;
      if (i == legacy_materials || seen_block[i] || !(row >> flag) || (row >> extra)) {
        return std::nullopt;
      }
      const auto parsed = color(value);
      if (!parsed || (flag != "breakable" && flag != "locked")) return std::nullopt;
      result.blocks[i] = {*parsed, flag == "breakable"};
      seen_block[i] = true;
    }
  }
  if (input.bad() || !input.eof() || !seen_sky || !seen_placement || !seen_block[1] ||
      !seen_block[2] || !seen_block[3])
    return std::nullopt;
  return result;
}

std::optional<Content> read_project_content(const std::filesystem::path& directory) {
  const auto file = directory / project_content;
  std::error_code error;
  const auto status = std::filesystem::symlink_status(file, error);
  if (status.type() == std::filesystem::file_type::not_found) return Content{};
  if (error || !std::filesystem::is_regular_file(status)) return std::nullopt;
  std::ifstream input(file, std::ios::binary);
  return read_content(input);
}

void write_content(std::ostream& output, const Content& content) {
  std::ostringstream encoded;
  encoded << signature << "\nsky " << std::hex << std::setfill('0') << std::setw(6) << content.sky
          << "\nplace " << block_name(content.placement) << '\n';
  for (std::size_t i = 1; i < legacy_materials; ++i) {
    const auto& style = content.blocks[i];
    encoded << names[i] << ' ' << std::setw(6) << style.color
            << (style.breakable ? " breakable\n" : " locked\n");
  }
  output << encoded.str();
}

}  // namespace wds::voxel
