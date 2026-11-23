#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string_view>

namespace wds::voxel {

enum class Block {
  air,
  grass,
  dirt,
  stone,
  copper,
  quartz,
  timber,
  rail,
  spring,
  water_low,
  water_mid,
  water_high,
  flame
};

[[nodiscard]] constexpr bool solid(Block block) {
  return block >= Block::grass && block <= Block::timber;
}

struct BlockStyle {
  std::uint32_t color;
  bool breakable;
  bool operator==(const BlockStyle&) const = default;
};

struct Content {
  std::uint32_t sky = 0x87bce8;
  Block placement = Block::stone;
  std::array<BlockStyle, 13> blocks{{{0, false},
                                     {0x659645, true},
                                     {0x90643e, true},
                                     {0x87929f, true},
                                     {0xad603f, true},
                                     {0xd2cbd6, true},
                                     {0x795239, true},
                                     {0x37324a, false},
                                     {0x196aa0, false},
                                     {0x8cdaf5, false},
                                     {0x58afdf, false},
                                     {0x3582d4, false},
                                     {0xf57b35, false}}};

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
