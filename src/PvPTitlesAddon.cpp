/*
 * mod-pvp-titles-ext - addon channel.
 *
 * Clients cannot read the server configuration, so an interface that wants
 * to show "honorable kills / next rank" has no way to know the thresholds.
 * The server whispers them to the player itself, with LANG_ADDON: the
 * client receives CHAT_MSG_ADDON (prefix PVPTITLES, distribution WHISPER).
 *
 *   server -> client  "RANKS:<k1>,...,<k14>"   lifetime honorable kills per rank
 *                     "DISHONOR:<left>,<kills>,<required>,<window>"
 *                                              (PvPTitlesDishonor.cpp)
 *   client -> server  "REQ"                    whispered to itself: answered
 *                                              with both messages, not echoed
 *
 * The login push may reach the client before its interface is loaded; the
 * request covers that case and /reload.
 */

#include "Chat.h"
#include "Configuration/Config.h"
#include "Player.h"
#include "PvPTitlesExt.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"

void PvPTitlesExt::SendAddonMessage(Player* player, std::string const& message)
{
    if (!player || !player->GetSession() || !sConfigMgr->GetOption<bool>("PvPTitles.AddonMessages", true))
        return;

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, std::string(ADDON_PREFIX) + "\t" + message);
    player->SendDirectMessage(&data);
}

void PvPTitlesExt::SendRanks(Player* player)
{
    std::string message = "RANKS:";
    for (uint8 rank = 1; rank <= 14; ++rank)
    {
        if (rank > 1)
            message += ",";
        message += std::to_string(PvPTitlesRequiredKills(rank));
    }
    SendAddonMessage(player, message);
}

class PvPTitlesAddon : public PlayerScript
{
public:
    PvPTitlesAddon() : PlayerScript("PvPTitlesAddon", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT
    }) { }

    using PlayerScript::OnPlayerCanUseChat;

    void OnPlayerLogin(Player* player) override
    {
        if (sConfigMgr->GetOption<bool>("PvPTitles.Enable", false))
            PvPTitlesExt::SendRanks(player);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 language, std::string& msg, Player* receiver) override
    {
        if (language != LANG_ADDON || receiver != player)
            return true;

        if (msg != std::string(PvPTitlesExt::ADDON_PREFIX) + "\tREQ")
            return true;

        if (sConfigMgr->GetOption<bool>("PvPTitles.Enable", false))
        {
            PvPTitlesExt::SendRanks(player);
            PvPTitlesExt::SendDishonorState(player);
        }

        // answered: the request is not echoed back
        return false;
    }
};

void AddPvpTitlesAddonScripts()
{
    new PvPTitlesAddon();
}
