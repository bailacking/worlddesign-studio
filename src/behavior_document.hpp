#pragma once

#include <json.hpp>

#include "behavior.hpp"

namespace wds::behavior {

struct LoadResult {
  std::optional<Program> program;
  std::string code;
  std::string location;
};

// Both source forms are parsed into ordered nodes before compilation.
LoadResult load_document(std::string_view format, const nlohmann::json& document);
nlohmann::json convert_document(std::string_view from, std::string_view to,
                                const nlohmann::json& document);

}  // namespace wds::behavior
