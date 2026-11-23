#pragma once
#include "ngba/settings.hpp"
#include <string>
namespace ngba {
bool save_settings(const Settings& settings, const std::string& path);
bool load_settings(Settings& settings, const std::string& path);
}
