#include "plugin_host.hpp"

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace wds;
using Trace = std::vector<std::string>;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

Plugin probe(std::string id, std::vector<std::string> dependencies, Trace& trace,
             bool required = true, bool fails = false, bool cleanup_fails = false) {
  return {id,
          {id},
          dependencies,
          required,
          [id, dependencies, &trace, fails, cleanup_fails](const Lookup& lookup, Cleanup& cleanup) {
            for (const auto& dependency : dependencies) {
              check(lookup(dependency) != nullptr, "dependency unavailable inside start");
            }
            trace.push_back("start " + id);
            cleanup.push_back([id, &trace] { trace.push_back("first " + id); });
            cleanup.push_back([id, &trace, cleanup_fails] {
              trace.push_back("second " + id);
              if (cleanup_fails) throw std::runtime_error("injected cleanup failure");
            });
            if (fails) throw std::runtime_error("injected initialization failure");
            return Capabilities{{id, id}};
          }};
}

void expect(const Trace& actual, std::initializer_list<std::string> expected) {
  check(actual == Trace(expected), "unexpected callback order");
}

}  // namespace

int main() {
  Trace trace;
  {
    PluginHost host({probe("commands", {"store"}, trace), probe("store", {}, trace)});
    check(host.start() == HostState::ready, "healthy composition must be ready");
    check(!host.disable("store") && host.capability("store"), "dependent must block disable");
    host.shutdown();
    check(!host.capability("commands"), "shutdown must unpublish commands");
    expect(trace, {"start store", "start commands", "second commands", "first commands",
                   "second store", "first store"});
  }
  trace.clear();
  {
    PluginHost host({probe("independent", {}, trace), probe("required", {"missing"}, trace)});
    check(host.start() == HostState::failed && trace.empty(), "missing required dependency");
  }
  {
    PluginHost host({probe("a", {"b"}, trace), probe("b", {"a"}, trace)});
    check(host.start() == HostState::failed && trace.empty(), "cycle must fail before start");
  }
  {
    auto duplicate = probe("b", {}, trace);
    duplicate.provides = {"a"};
    PluginHost host({probe("a", {}, trace), duplicate});
    check(host.start() == HostState::failed && trace.empty(), "ambiguous capability");
  }
  {
    PluginHost host({probe("store", {}, trace), probe("commands", {"store"}, trace, true, true)});
    check(host.start() == HostState::failed && !host.capability("store"), "required failure");
    expect(trace, {"start store", "start commands", "second commands", "first commands",
                   "second store", "first store"});
  }
  trace.clear();
  {
    PluginHost host({probe("a", {}, trace), probe("b", {}, trace, false, true),
                     probe("c", {"b"}, trace, false)});
    check(host.start() == HostState::degraded, "optional failure must degrade");
    check(host.capability("a") && !host.capability("b") && !host.capability("c"),
          "only independent healthy capability must remain available");
    expect(trace, {"start a", "start b", "second b", "first b"});
  }
  trace.clear();
  {
    PluginHost host({probe("a", {}, trace, true, false, true)});
    check(host.start() == HostState::ready, "cleanup injection must start");
    host.shutdown();
    check(host.start() == HostState::failed, "cleanup failure must block restart");
    expect(trace, {"start a", "second a", "first a"});
  }
}
