#pragma once

#include <string>
#include <nlohmann/json.hpp>

// Simple, stateless inspector API:
//   DwgInspector inspector;
//   nlohmann::json j = inspector.inspect("file.dwg");
class DwgInspector {
public:
    // Inspect the given DWG file and return a JSON object.
    // Throws std::runtime_error on failure.
    nlohmann::json inspect(const std::string& dwg_path);
};
