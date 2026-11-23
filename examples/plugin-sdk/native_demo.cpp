#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "palette_abi.h"
#include "plugin_host.hpp"

namespace {

constexpr auto capability = "example.palette";

class Library {
 public:
  explicit Library(const std::filesystem::path& file) {
    const auto path = std::filesystem::absolute(file);
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("invalid_library");
#ifdef _WIN32
    library_ = LoadLibraryExW(path.c_str(), nullptr,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    library_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (!library_) throw std::runtime_error("invalid_library");
  }

  ~Library() {
#ifdef _WIN32
    FreeLibrary(library_);
#else
    dlclose(library_);
#endif
  }

  Library(const Library&) = delete;
  Library& operator=(const Library&) = delete;

  template <typename Function>
  Function symbol(const char* name) const {
#ifdef _WIN32
    const auto address = GetProcAddress(library_, name);
#else
    const auto address = dlsym(library_, name);
#endif
    if (!address) throw std::runtime_error(std::string("missing_symbol ") + name);
    return reinterpret_cast<Function>(address);
  }

 private:
#ifdef _WIN32
  HMODULE library_ = nullptr;
#else
  void* library_ = nullptr;
#endif
};

uint32_t preview(const std::filesystem::path& path) {
  const Library library(path);
  const auto version =
      library.symbol<decltype(&wds_palette_abi_version)>("wds_palette_abi_version");
  if (version() != WDS_PALETTE_ABI_VERSION) throw std::runtime_error("abi_mismatch");
  using MapRgb = decltype(&wds_palette_map_rgb);
  const auto color = library.symbol<MapRgb>("wds_palette_map_rgb");
  wds::PluginHost host(
      {{"native-palette", {capability}, {}, true, [color](const wds::Lookup&, wds::Cleanup&) {
          return wds::Capabilities{{capability, color}};
        }}});
  if (host.start() != wds::HostState::ready) throw std::runtime_error("plugin_start_failed");
  const auto* provider = std::any_cast<MapRgb>(host.capability(capability));
  if (!provider) throw std::runtime_error("provider_missing");
  const auto rgb = (*provider)(0x526980);
  if (rgb > 0xffffffu) throw std::runtime_error("invalid_color");
  return rgb;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
  if (argc != 4) return 1;
  try {
    if (preview(argv[1]) != 0x5269c0 || preview(argv[2]) != 0x526960) return 2;
    try {
      (void)preview(argv[3]);
      return 3;
    } catch (const std::runtime_error& error) {
      if (std::string_view(error.what()) != "abi_mismatch") throw;
      std::cout << "Rejected native palette ABI version 0\n";
    }
    if (preview(argv[1]) != 0x5269c0) return 4;
    std::cout << "Native SDK module loaded, replaced, checked for compatibility and restored\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 5;
  }
}
