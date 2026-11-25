#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "plugin_host.hpp"

namespace {

using namespace wds;

// Both providers share this example contract at build time.
struct Palette {
  int version;
  std::string tone;
};

Plugin palette(std::string id, Palette data) {
  return {std::move(id),
          {"example.palette"},
          {},
          true,
          [data = std::move(data)](const Lookup&, Cleanup&) {
            return Capabilities{{"example.palette", data}};
          }};
}

Plugin preview(std::string provider, bool required = true) {
  return {"preview",
          {"example.preview"},
          {std::move(provider)},
          required,
          [](const Lookup& lookup, Cleanup&) {
            const auto* data = std::any_cast<Palette>(lookup("example.palette"));
            if (!data || data->version != 1)
              throw std::runtime_error("example.palette requires contract version 1");
            return Capabilities{{"example.preview", "Preview: " + data->tone}};
          }};
}

std::string compose(Plugin provider, bool verify_lifecycle = false) {
  const auto id = provider.id;
  PluginHost host({preview(id, !verify_lifecycle), std::move(provider)});
  if (host.start() != HostState::ready) throw std::runtime_error("Sample failed to start");
  const auto output = std::any_cast<std::string>(*host.capability("example.preview"));
  if (verify_lifecycle && (host.disable(id) || !host.disable("preview") || !host.disable(id) ||
                           host.capability("example.palette")))
    throw std::runtime_error("Provider and dependent did not disable in order");
  return output;
}

}  // namespace

int main() {
  try {
    if (compose(palette("daylight", {1, "daylight"}), true) != "Preview: daylight" ||
        compose(palette("overcast", {1, "overcast"})) != "Preview: overcast")
      return 1;
    {
      PluginHost host({preview("obsolete"), palette("obsolete", {0, "obsolete"})});
      if (host.start() != HostState::failed || host.capability("example.palette")) return 2;
      bool reported = false;
      for (const auto& diagnostic : host.diagnostics()) {
        std::cout << diagnostic.plugin << " / " << diagnostic.phase << " / " << diagnostic.message
                  << '\n';
        reported |= diagnostic.plugin == "preview" &&
                    diagnostic.message.find("requires contract version 1") != std::string::npos;
      }
      if (!reported) return 3;
    }
    if (compose(palette("daylight", {1, "daylight"})) != "Preview: daylight") return 4;
    std::cout << "Installed source SDK: enabled, replaced, rejected incompatible, restored\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 5;
  }
}
