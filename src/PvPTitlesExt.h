#ifndef MOD_PVP_TITLES_EXT_H
#define MOD_PVP_TITLES_EXT_H

#include "Define.h"
#include <string>

class Player;

// Rank thresholds read from the configuration (mod_pvp_titles.cpp):
// lifetime honorable kills required for rank 1..14, 0 outside that range.
uint32 PvPTitlesRequiredKills(uint8 rank);

namespace PvPTitlesExt
{
    // Addon channel: the server whispers the player with LANG_ADDON, the
    // client receives CHAT_MSG_ADDON with this prefix. The client asks for a
    // refresh by whispering itself "REQ" with the same prefix.
    constexpr char const* ADDON_PREFIX = "PVPTITLES";

    void SendAddonMessage(Player* player, std::string const& message);

    // "RANKS:<k1>,...,<k14>"
    void SendRanks(Player* player);

    // "DISHONOR:<seconds left>,<kills in window>,<kills required>,<window>"
    // (PvPTitlesDishonor.cpp)
    void SendDishonorState(Player* player);

    // "CIV:<entry>:<0|1>" -- whether the creature template carries
    // CREATURE_FLAG_EXTRA_CIVILIAN, 0 for an unknown entry
    void SendCivilian(Player* player, uint32 entry);
}

#endif
