#pragma once

#include <any>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace wds {

using Capabilities = std::map<std::string, std::any>;
using Cleanup = std::vector<std::function<void()>>;
using Lookup = std::function<const std::any*(const std::string&)>;

struct Plugin {
  std::string id;
  std::vector<std::string> provides;
  std::vector<std::string> dependencies;
  bool required;
  std::function<Capabilities(const Lookup&, Cleanup&)> start;
};

enum class HostState { stopped, ready, degraded, failed };

struct PluginDiagnostic {
  std::string plugin;
  std::string phase;
  std::string message;
};

// One synchronous owner. Callbacks may use their lookup only during start.
class PluginHost {
 public:
  explicit PluginHost(std::vector<Plugin> plugins);
  ~PluginHost();
  PluginHost(const PluginHost&) = delete;
  PluginHost& operator=(const PluginHost&) = delete;

  HostState start();
  bool disable(const std::string& id);
  void shutdown();
  [[nodiscard]] const std::any* capability(const std::string& name) const;
  [[nodiscard]] const std::vector<PluginDiagnostic>& diagnostics() const;

 private:
  struct Active {
    const Plugin* plugin;
    Capabilities capabilities;
    Cleanup cleanup;
  };

  bool cleanup(const std::string& id, Cleanup& actions);
  void stop_at(std::size_t index);
  void report(const std::string& id, const std::string& phase, std::string message);

  std::vector<Plugin> plugins_;
  std::vector<Active> active_;
  std::vector<PluginDiagnostic> diagnostics_;
  HostState state_ = HostState::stopped;
  bool restart_required_ = false;
};

}  // namespace wds
