#pragma once

#include "plugin_host.hpp"
#include "project_store.hpp"

namespace wds {

class Workspace {
 public:
  explicit Workspace(const std::filesystem::path& path);
  RenameResult open();
  RenameResult rename(const RenameRequest& request, bool writable = true);

 private:
  RenameResult startup_failure() const;
  PluginHost host_;
};

}  // namespace wds
