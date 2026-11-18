#include "voxel_palette.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

#include "plugin_host.hpp"

namespace wds::voxel {
namespace {

constexpr std::array<std::string_view, 2> providers{"daylight", "amber-dusk"};
constexpr auto capability = "voxel.palette";

std::uint32_t dusk(std::uint32_t color) {
  const auto red = ((color >> 16U) & 255U) * 4 / 5 + 25;
  const auto green = ((color >> 8U) & 255U) * 3 / 5 + 14;
  const auto blue = (color & 255U) * 2 / 3 + 18;
  return (red << 16U) | (green << 8U) | blue;
}

}  // namespace

bool palette_supported(std::string_view provider) {
  return std::ranges::find(providers, provider) != providers.end();
}

std::optional<Content> use_palette(std::string_view provider, Content original) {
  if (!palette_supported(provider)) return {};
  PluginHost host({{std::string(provider),
                    {capability},
                    {},
                    true,
                    [provider, original](const Lookup&, Cleanup&) mutable {
                      if (provider == "amber-dusk") {
                        original.sky = dusk(original.sky);
                        for (std::size_t i = 1; i < original.blocks.size(); ++i)
                          original.blocks[i].color = dusk(original.blocks[i].color);
                      }
                      return Capabilities{{capability, original}};
                    }}});
  if (host.start() != HostState::ready) return {};
  const auto* active = std::any_cast<Content>(host.capability(capability));
  return active ? std::optional(*active) : std::nullopt;
}

}  // namespace wds::voxel
