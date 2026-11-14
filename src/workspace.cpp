#include "workspace.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace wds {
namespace {

using Store = std::shared_ptr<ProjectStore>;
using Commands = std::function<RenameResult(const RenameRequest&, bool)>;

}  // namespace

Workspace::Workspace(const std::filesystem::path& path)
    : host_({
          {"project-store",
           {"project-store"},
           {},
           true,
           [path](const Lookup&, Cleanup&) {
             return Capabilities{{"project-store", std::make_shared<ProjectStore>(path)}};
           }},
          {"asset-commands",
           {"asset.rename"},
           {"project-store"},
           true,
           [](const Lookup& lookup, Cleanup&) {
             const auto* capability = lookup("project-store");
             if (!capability) throw std::runtime_error("project-store unavailable");
             auto store = std::any_cast<Store>(*capability);
             Commands rename = [store](const RenameRequest& request, bool writable) {
               return store->rename(request, writable);
             };
             return Capabilities{{"asset.rename", std::move(rename)}};
           }},
      }) {
  host_.start();
}

RenameResult Workspace::startup_failure() const {
  std::string detail = "workspace startup failed";
  for (const auto& diagnostic : host_.diagnostics()) {
    detail += "\n" + diagnostic.plugin + " " + diagnostic.phase + ": " + diagnostic.message;
  }
  return {RenameStatus::recovery_required, 0, false, {}, {}, {}, std::move(detail)};
}

RenameResult Workspace::open() {
  const auto* capability = host_.capability("project-store");
  return capability ? std::any_cast<const Store&>(*capability)->open() : startup_failure();
}

RenameResult Workspace::rename(const RenameRequest& request, bool writable) {
  const auto* capability = host_.capability("asset.rename");
  return capability ? std::any_cast<const Commands&>(*capability)(request, writable)
                    : startup_failure();
}

}  // namespace wds
