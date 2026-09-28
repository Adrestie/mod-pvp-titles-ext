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
 *                     "CIV:<entry>:<0|1>"      whether that creature is a civilian
 *   client -> server  "REQ"                    whispered to itself: answered
 *                                              with both messages, not echoed
 *                     "CIV:<entry>"            answered with "CIV:<entry>:<0|1>",
 *                                              not echoed
 *
 * The login push may reach the client before its interface is loaded; the
 * request covers that case and /reload.
 *
 * The civilian flag (CREATURE_FLAG_EXTRA_CIVILIAN) is server data: the 3.3.5
 * creature query response does not carry it, so an interface that wants to
 * mark civilians in its tooltips asks for the creature entry it shows.
 */

#include "Chat.h"
#include "Configuration/Config.h"
#include "CreatureData.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PvPTitlesExt.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
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

void PvPTitlesExt::SendCivilian(Player* player, uint32 entry)
{
    CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(entry);
    bool civilian = creature && (creature->flags_extra & CREATURE_FLAG_EXTRA_CIVILIAN);
    SendAddonMessage(player, "CIV:" + std::to_string(entry) + ":" + (civilian ? "1" : "0"));
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

        std::string const prefix = std::string(PvPTitlesExt::ADDON_PREFIX) + "\t";
        if (msg.compare(0, prefix.size(), prefix) != 0)
            return true;

        std::string const request = msg.substr(prefix.size());
        bool const enabled = sConfigMgr->GetOption<bool>("PvPTitles.Enable", false);

        if (request == "REQ")
        {
            if (enabled)
            {
                PvPTitlesExt::SendRanks(player);
                PvPTitlesExt::SendDishonorState(player);
            }
        }
        else if (request.compare(0, 4, "CIV:") == 0)
        {
            if (enabled)
                if (Optional<uint32> entry = Acore::StringTo<uint32>(request.substr(4), 10))
                    PvPTitlesExt::SendCivilian(player, *entry);
        }
        else
            return true;

        // answered: the request is not echoed back
        return false;
    }
};

void AddPvpTitlesAddonScripts()
{
    new PvPTitlesAddon();
}
