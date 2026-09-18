/* The registry, as a map the driver fills from the chimera settings.
 *
 * AppleWin keeps its configuration in the Windows registry and reads it back
 * through these four calls at LoadConfiguration(). Here the driver puts the
 * machine's configuration - the model, the cards in each slot, the video
 * mode, the joysticks - into the map BEFORE the emulator is built, and the
 * emulator reads it as it would the registry. What the emulator writes back
 * (it saves what it decided, e.g. a card type it inserted) lands in the same
 * map and goes nowhere else.
 */
#include "StdAfx.h"
#include "Registry.h"
#include "chimera-registry.h"

#include <map>
#include <string>

namespace
{
    std::map<std::string, std::string> &values()
    {
        static std::map<std::string, std::string> map;
        return map;
    }

    std::string keyOf(LPCTSTR section, LPCTSTR key)
    {
        return std::string(section) + "\\" + key;
    }
} // namespace

void chimera_registry_put(const char *section, const char *key, const std::string &value)
{
    values()[keyOf(section, key)] = value;
}

void chimera_registry_put(const char *section, const char *key, uint32_t value)
{
    values()[keyOf(section, key)] = std::to_string(value);
}

bool chimera_registry_get(const char *section, const char *key, std::string &value)
{
    auto it = values().find(keyOf(section, key));
    if (it == values().end())
        return false;
    value = it->second;
    return true;
}

bool RegLoadString(LPCTSTR section, LPCTSTR key, bool, LPTSTR buffer, uint32_t chars)
{
    std::string value;
    if (!chimera_registry_get(section, key, value))
        return false;
    strncpy(buffer, value.c_str(), chars);
    if (chars)
        buffer[chars - 1] = 0;
    return true;
}

bool RegLoadString(LPCTSTR section, LPCTSTR key, bool peruser, LPTSTR buffer, uint32_t chars, LPCTSTR defaultValue)
{
    const bool found = RegLoadString(section, key, peruser, buffer, chars);
    if (!found)
        StringCbCopy(buffer, chars, defaultValue);
    return found;
}

bool RegLoadValue(LPCTSTR section, LPCTSTR key, bool, uint32_t *value)
{
    std::string text;
    if (!chimera_registry_get(section, key, text))
        return false;
    *value = (uint32_t)strtoul(text.c_str(), nullptr, 10);
    return true;
}

bool RegLoadValue(LPCTSTR section, LPCTSTR key, bool peruser, uint32_t *value, uint32_t defaultValue)
{
    const bool found = RegLoadValue(section, key, peruser, value);
    if (!found)
        *value = defaultValue;
    return found;
}

void RegSaveString(LPCTSTR section, LPCTSTR key, bool, const std::string &buffer)
{
    chimera_registry_put(section, key, buffer);
}

void RegSaveValue(LPCTSTR section, LPCTSTR key, bool, uint32_t value)
{
    chimera_registry_put(section, key, value);
}

//===========================================================================
// the slot sections, as upstream's Registry.cpp computes them

static inline std::string RegGetSlotSection(UINT slot)
{
    return (slot == SLOT_AUX) ? std::string(REG_CONFIG_SLOT_AUX) : (std::string(REG_CONFIG_SLOT) + (char)('0' + slot));
}

std::string RegGetConfigSlotSection(UINT slot)
{
    return std::string(REG_CONFIG "\\") + RegGetSlotSection(slot);
}

void RegDeleteConfigSlotSection(UINT slot)
{
    const std::string prefix = RegGetConfigSlotSection(slot) + "\\";
    auto &map = values();
    for (auto it = map.begin(); it != map.end();)
    {
        if (it->first.compare(0, prefix.size(), prefix) == 0)
            it = map.erase(it);
        else
            ++it;
    }
}

void RegSetConfigSlotNewCardType(UINT slot, SS_CARDTYPE type)
{
    RegDeleteConfigSlotSection(slot);
    RegSaveValue(RegGetConfigSlotSection(slot).c_str(), REGVALUE_CARD_TYPE, true, type);
}

void RegSetConfigGameIOConnectorNewDongleType(UINT slot, DONGLETYPE type)
{
    if (slot != GAME_IO_CONNECTOR)
        return;
    RegDeleteConfigSlotSection(slot);
    RegSaveValue(RegGetConfigSlotSection(slot).c_str(), REGVALUE_GAME_IO_TYPE, true, type);
}
