#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string_view>

namespace wds::voxel {

enum class Block { air, grass, dirt, stone };

struct BlockStyle {
  std::uint32_t color;
  bool breakable;
  bool operator==(const BlockStyle&) const = default;
};

struct Content {
  std::uint32_t sky = 0x87bce8;
  Block placement = Block::stone;
  std::array<BlockStyle, 4> blocks{
      {{0, false}, {0x659645, true}, {0x90643e, true}, {0x87929f, true}}};

  [[nodiscard]] const BlockStyle& style(Block block) const;
  bool operator==(const Content&) const = default;
};

inline constexpr auto project_content = "sandbox-content-v1.txt";

[[nodiscard]] std::string_view block_name(Block block);
[[nodiscard]] std::optional<Content> template_content(std::string_view name);
[[nodiscard]] std::optional<Content> read_content(std::istream& input);
[[nodiscard]] std::optional<Content> read_project_content(const std::filesystem::path& directory);
void write_content(std::ostream& output, const Content& content);

}  // namespace wds::voxel
