#include "plugin_host.hpp"

#include <algorithm>
#include <ranges>
#include <set>
#include <stdexcept>
#include <utility>

namespace wds {
namespace {

bool needs(const Plugin& plugin, const std::string& dependency) {
  return std::ranges::find(plugin.dependencies, dependency) != plugin.dependencies.end();
}

}  // namespace

PluginHost::PluginHost(std::vector<Plugin> plugins) : plugins_(std::move(plugins)) {
  std::ranges::sort(plugins_, {}, &Plugin::id);
  active_.reserve(plugins_.size());
}

PluginHost::~PluginHost() { shutdown(); }

void PluginHost::report(const std::string& id, const std::string& phase, std::string message) {
  diagnostics_.push_back({id, phase, std::move(message)});
}

bool PluginHost::cleanup(const std::string& id, Cleanup& actions) {
  bool success = true;
  while (!actions.empty()) {
    auto action = std::move(actions.back());
    actions.pop_back();
    try {
      action();
    } catch (...) {
      restart_required_ = true;
      success = false;
      report(id, "cleanup", "action failed; restart required");
    }
  }
  return success;
}

HostState PluginHost::start() {
  if (state_ == HostState::ready || state_ == HostState::degraded) return state_;
  if (restart_required_) return HostState::failed;
  diagnostics_.clear();
  state_ = HostState::failed;

  std::map<std::string, const Plugin*> by_id;
  std::set<std::string> provided;
  for (const auto& plugin : plugins_) {
    if (plugin.id.empty() || !plugin.start || !by_id.emplace(plugin.id, &plugin).second) {
      report(plugin.id, "preflight", "invalid or duplicate plugin");
      return state_;
    }
    for (const auto& name : plugin.provides) {
      if (name.empty() || !provided.insert(name).second) {
        report(plugin.id, "preflight", "invalid or duplicate capability: " + name);
        return state_;
      }
    }
  }

  std::vector<const Plugin*> order;
  std::set<std::string> ordered;
  while (order.size() != plugins_.size()) {
    std::vector<const Plugin*> layer;
    for (const auto& plugin : plugins_) {
      if (ordered.contains(plugin.id)) continue;
      if (std::ranges::all_of(plugin.dependencies, [&](const auto& id) {
            return !by_id.contains(id) || ordered.contains(id);
          })) {
        layer.push_back(&plugin);
      }
    }
    if (layer.empty()) {
      for (const auto& plugin : plugins_) {
        if (!ordered.contains(plugin.id))
          report(plugin.id, "preflight", "dependency cycle or blocked by one");
      }
      return state_;
    }
    for (const auto* plugin : layer) {
      ordered.insert(plugin->id);
      order.push_back(plugin);
    }
  }

  std::set<std::string> unavailable;
  bool required_missing = false;
  for (const auto* plugin : order) {
    for (const auto& id : plugin->dependencies) {
      if (by_id.contains(id) && !unavailable.contains(id)) continue;
      unavailable.insert(plugin->id);
      report(plugin->id, "preflight", "unavailable dependency: " + id);
      required_missing = required_missing || plugin->required;
      break;
    }
  }
  if (required_missing) return state_;

  for (const auto* plugin : order) {
    if (unavailable.contains(plugin->id)) continue;
    const auto blocked = std::ranges::find_if(
        plugin->dependencies, [&](const auto& id) { return unavailable.contains(id); });
    if (blocked != plugin->dependencies.end()) {
      report(plugin->id, "start", "dependency failed: " + *blocked);
      unavailable.insert(plugin->id);
    } else {
      Cleanup actions;
      try {
        auto offered = plugin->start(
            [&](const std::string& name) -> const std::any* {
              for (const auto& running : active_) {
                if (!needs(*plugin, running.plugin->id)) continue;
                const auto match = running.capabilities.find(name);
                if (match != running.capabilities.end()) return &match->second;
              }
              return nullptr;
            },
            actions);
        if (offered.size() != plugin->provides.size() ||
            !std::ranges::all_of(plugin->provides, [&](const auto& name) {
              const auto it = offered.find(name);
              return it != offered.end() && it->second.has_value();
            })) {
          throw std::runtime_error("published capabilities do not match declaration");
        }
        active_.push_back({plugin, std::move(offered), std::move(actions)});
        report(plugin->id, "start", "ready");
        continue;
      } catch (const std::exception& error) {
        cleanup(plugin->id, actions);
        report(plugin->id, "start", error.what());
      } catch (...) {
        cleanup(plugin->id, actions);
        report(plugin->id, "start", "unknown initialization error");
      }
      unavailable.insert(plugin->id);
    }
    if (plugin->required || restart_required_) {
      shutdown();
      state_ = HostState::failed;
      return state_;
    }
  }

  state_ = unavailable.empty() ? HostState::ready : HostState::degraded;
  return state_;
}

void PluginHost::stop_at(std::size_t index) {
  auto& plugin = active_[index];
  const auto& id = plugin.plugin->id;
  plugin.capabilities.clear();
  const bool success = cleanup(id, plugin.cleanup);
  report(id, "stop", success ? "stopped" : "cleanup failed");
  active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(index));
}

bool PluginHost::disable(const std::string& id) {
  const auto target =
      std::ranges::find_if(active_, [&](const auto& active) { return active.plugin->id == id; });
  if (target == active_.end()) return false;
  for (const auto& active : active_) {
    if (needs(*active.plugin, id)) {
      report(id, "stop", "active dependent: " + active.plugin->id);
      return false;
    }
  }
  const bool required = target->plugin->required;
  stop_at(static_cast<std::size_t>(target - active_.begin()));
  if (restart_required_ || required) {
    shutdown();
    if (restart_required_) state_ = HostState::failed;
  } else {
    state_ = HostState::degraded;
  }
  return !restart_required_;
}

void PluginHost::shutdown() {
  state_ = HostState::stopped;
  while (!active_.empty()) stop_at(active_.size() - 1);
  if (restart_required_) state_ = HostState::failed;
}

const std::any* PluginHost::capability(const std::string& name) const {
  if (state_ != HostState::ready && state_ != HostState::degraded) return nullptr;
  for (const auto& active : active_) {
    const auto it = active.capabilities.find(name);
    if (it != active.capabilities.end()) return &it->second;
  }
  return nullptr;
}

const std::vector<PluginDiagnostic>& PluginHost::diagnostics() const { return diagnostics_; }

}  // namespace wds
