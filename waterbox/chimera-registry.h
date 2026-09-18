/* the driver's side of waterbox/shims/Registry.cpp */
#pragma once

#include <cstdint>
#include <string>

void chimera_registry_put(const char *section, const char *key, const std::string &value);
void chimera_registry_put(const char *section, const char *key, uint32_t value);
bool chimera_registry_get(const char *section, const char *key, std::string &value);
