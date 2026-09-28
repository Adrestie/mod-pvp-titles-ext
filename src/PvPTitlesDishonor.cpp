/*
 * mod-pvp-titles-ext - the Dishonored state.
 *
 * The core no longer tracks dishonorable kills. This brings back the old
 * punishment for killing the non-combatants other players depend on:
 *
 *   - a dishonorable kill is a creature flagged CREATURE_FLAG_EXTRA_CIVILIAN
 *     whose (original) faction belongs to the killer's enemy side; triggers,
 *     critters, totems and player-controlled units never count. The killing
 *     blow counts, the killer's pets included;
 *   - PvPTitles.Dishonor.Kills of them within PvPTitles.Dishonor.Window
 *     seconds make the player Dishonored for PvPTitles.Dishonor.Duration
 *     seconds of real time (offline time counts); another dishonorable kill
 *     while Dishonored starts the duration again.
 *
 * While Dishonored (suspended in battlegrounds and arenas):
 *   - every faction with a reputation is forced hostile to him
 *     (ReputationMgr forced reactions, sent to his client): guards, vendors,
 *     his own capitals included;
 *   - creatures without a reputation that would be friendly to him are
 *     hostile too (IfNormalReaction), and shown hostile to him
 *     (OnPatchValuesUpdate: faction template 14, "Monster"). Spirit healers
 *     and spirit guides are spared, so that he can still come back to life;
 *   - every player outside his group may attack him and he may attack them
 *     back: the server holds them hostile to each other (IfNormalReaction),
 *     and each client of his own side is shown him - and he is shown them -
 *     with an enemy player faction template (OnPatchValuesUpdate), the way the
 *     core already shows mixed-faction group members as friends. He stays
 *     flagged for PvP.
 *
 * Forced reactions coming from auras (SPELL_AURA_FORCE_REACTION) share the
 * same map: those on a faction with a reputation are lost when the state ends.
 */

#include "CellImpl.h"
#include "Chat.h"
#include "Configuration/Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "PvPTitlesExt.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "Util.h"
#include <deque>
#include <limits>
#include <list>
#include <unordered_map>

namespace
{
    // FactionTemplate.dbc: an enemy player for each side, and a monster
    constexpr uint32 FACTION_TEMPLATE_PLAYER_HUMAN = 1;
    constexpr uint32 FACTION_TEMPLATE_PLAYER_ORC = 2;
    constexpr uint32 FACTION_TEMPLATE_MONSTER = 14;

    constexpr uint32 CHECK_INTERVAL = 1000;

    struct DishonorState
    {
        std::deque<uint32> kills;   // dishonorable kills within the window (unix time)
        uint32 expires = 0;         // end of the Dishonored state (unix time), 0 = none
        bool applied = false;       // effects in force (suspended in battlegrounds)
        bool auraSynced = false;    // the debuff's time set since login
        uint32 checkTimer = 0;
    };

    // online players with a state (Dishonored, or dishonorable kills pending)
    std::unordered_map<ObjectGuid::LowType, DishonorState> States;

    // players whose effects are in force: the per-observer hooks leave at once
    // while it is zero
    uint32 DishonoredOnline = 0;

    bool Enabled()
    {
        return sConfigMgr->GetOption<bool>("PvPTitles.Enable", false)
            && sConfigMgr->GetOption<bool>("PvPTitles.Dishonor.Enable", true);
    }

    uint32 KillsRequired()
    {
        return std::max<uint32>(1, sConfigMgr->GetOption<uint32>("PvPTitles.Dishonor.Kills", 5));
    }

    uint32 Window()
    {
        return sConfigMgr->GetOption<uint32>("PvPTitles.Dishonor.Window", DAY);
    }

    uint32 Duration()
    {
        return sConfigMgr->GetOption<uint32>("PvPTitles.Dishonor.Duration", DAY);
    }

    uint32 Now()
    {
        return uint32(GameTime::GetGameTime().count());
    }

    // AN AURA ON THE DISHONORED PLAYER, 0 (the default) for none. A client
    // shows an aura only if its own Spell.dbc knows the spell, and no spell of
    // the original client has a fitting name, tooltip and no visual effect: by
    // default the debuff is left to addons, which draw it from the DISHONOR
    // message (PvPTitlesAddon.cpp). A server that ships a client patch can put
    // its own spell here.
    uint32 AuraSpell()
    {
        return sConfigMgr->GetOption<uint32>("PvPTitles.Dishonor.AuraSpell", 0);
    }

    // the debuff carries the time left, which the client shows under its
    // icon; gone when the state is. Only the module's own: the one the player
    // casts on himself -- Parqual Fintallas (Test of Lore) puts Mark of Shame
    // on players too, and that one is his
    void UpdateAura(Player* player, uint32 expires, uint32 now)
    {
        uint32 const spellId = AuraSpell();
        if (!spellId || !sSpellMgr->GetSpellInfo(spellId))
            return;

        ObjectGuid const own = player->GetGUID();
        if (expires <= now)
        {
            player->RemoveAurasDueToSpell(spellId, own);
            return;
        }

        Aura* aura = player->GetAura(spellId, own);
        if (!aura)
            aura = player->AddAura(spellId, player);
        if (!aura)
            return;

        int32 const left = int32(std::min<uint64>(uint64(expires - now) * IN_MILLISECONDS, std::numeric_limits<int32>::max()));
        aura->SetMaxDuration(left);
        aura->SetDuration(left);
    }

    DishonorState* Find(Player const* player)
    {
        if (!player)
            return nullptr;

        auto itr = States.find(player->GetGUID().GetCounter());
        return itr == States.end() ? nullptr : &itr->second;
    }

    bool IsDishonored(Player const* player)
    {
        if (!DishonoredOnline || !player)
            return false;

        DishonorState const* state = Find(player);
        return state && state->applied;
    }

    void Prune(DishonorState& state, uint32 now)
    {
        uint32 const window = Window();
        while (!state.kills.empty() && state.kills.front() + window <= now)
            state.kills.pop_front();
    }

    bool IsDishonorableVictim(Player const* killer, Creature const* victim)
    {
        if (!victim->IsCivilian() || victim->IsTrigger() || victim->IsCritter() || victim->IsTotem()
            || victim->IsControlledByPlayer())
            return false;

        FactionTemplateEntry const* faction = sFactionTemplateStore.LookupEntry(victim->GetCreatureTemplate()->faction);
        if (!faction)
            return false;

        uint32 const own = killer->GetTeamId(true) == TEAM_ALLIANCE ? FACTION_MASK_ALLIANCE : FACTION_MASK_HORDE;
        uint32 const enemy = own == FACTION_MASK_ALLIANCE ? FACTION_MASK_HORDE : FACTION_MASK_ALLIANCE;
        uint32 const side = faction->ourMask | faction->friendlyMask;
        return (side & enemy) && !(side & own);
    }

    // creatures the state leaves alone
    bool IsSpared(Unit const* unit)
    {
        if (unit->IsCritter())
            return true;

        if (Creature const* creature = unit->ToCreature())
            return creature->IsTrigger() || creature->HasNpcFlag(NPCFlags(UNIT_NPC_FLAG_SPIRITHEALER | UNIT_NPC_FLAG_SPIRITGUIDE));

        return false;
    }

    class AnyUnitInRange
    {
    public:
        AnyUnitInRange(WorldObject const* center, float range) : _center(center), _range(range) { }

        bool operator()(Unit* unit) const
        {
            return _center->IsWithinDistInMap(unit, _range);
        }

    private:
        WorldObject const* _center;
        float _range;
    };

    // what he sees of others and what others see of him changes: resend the
    // faction template of every unit around (OnPatchValuesUpdate rewrites it
    // for each observer)
    void RefreshViews(Player* player)
    {
        float const range = player->GetVisibilityRange();
        std::list<Unit*> units;
        AnyUnitInRange check(player, range);
        Acore::UnitListSearcher<AnyUnitInRange> searcher(player, units, check);
        Cell::VisitObjects(player, searcher, range);

        player->ForceValuesUpdateAtIndex(UNIT_FIELD_FACTIONTEMPLATE);
        for (Unit* unit : units)
            if (unit != player)
                unit->ForceValuesUpdateAtIndex(UNIT_FIELD_FACTIONTEMPLATE);
    }

    void SetEffects(Player* player, DishonorState& state, bool apply)
    {
        if (state.applied == apply)
            return;

        state.applied = apply;
        if (apply)
            ++DishonoredOnline;
        else
            --DishonoredOnline;

        ReputationMgr& reputation = player->GetReputationMgr();
        for (uint32 id = 0; id < sFactionStore.GetNumRows(); ++id)
            if (FactionEntry const* faction = sFactionStore.LookupEntry(id))
                if (faction->CanHaveReputation())
                    reputation.ApplyForceReaction(faction->ID, REP_HOSTILE, apply);
        reputation.SendForceReactions();

        // flagged for PvP while it lasts; afterwards the usual five minutes
        player->UpdatePvP(true, apply);

        RefreshViews(player);
    }

    bool ShouldApply(Player const* player, DishonorState const& state, uint32 now)
    {
        return state.expires > now && !player->InBattleground() && !player->InArena();
    }

    void Save(Player const* player, DishonorState const& state)
    {
        ObjectGuid::LowType const guid = player->GetGUID().GetCounter();
        if (state.expires)
            CharacterDatabase.Execute("REPLACE INTO `mod_pvp_titles_dishonor` (`guid`, `expires`) VALUES ({}, {})", guid, state.expires);
        else
            CharacterDatabase.Execute("DELETE FROM `mod_pvp_titles_dishonor` WHERE `guid` = {}", guid);
    }

    void Dishonor(Player* player, DishonorState& state, uint32 now)
    {
        state.expires = now + Duration();
        state.kills.clear();
        Save(player, state);
        CharacterDatabase.Execute("DELETE FROM `mod_pvp_titles_dishonorable_kills` WHERE `guid` = {}", player->GetGUID().GetCounter());

        ChatHandler(player->GetSession()).PSendSysMessage("You are Dishonored for {}: every faction, your own included, is hostile to you, and any player may attack you.",
            secsToTimeString(Duration()));

        UpdateAura(player, state.expires, now);
        state.auraSynced = true;

        if (ShouldApply(player, state, now))
            SetEffects(player, state, true);

        PvPTitlesExt::SendDishonorState(player);
    }

    void Expire(Player* player, DishonorState& state)
    {
        state.expires = 0;
        UpdateAura(player, 0, Now());
        SetEffects(player, state, false);
        Save(player, state);
        ChatHandler(player->GetSession()).SendSysMessage("You are no longer Dishonored.");
        PvPTitlesExt::SendDishonorState(player);
    }

    void OnDishonorableKill(Player* killer)
    {
        uint32 const now = Now();
        ObjectGuid::LowType const guid = killer->GetGUID().GetCounter();
        DishonorState& state = States[guid];
        Prune(state, now);

        if (state.expires > now)
        {
            Dishonor(killer, state, now);
            return;
        }

        state.kills.push_back(now);
        uint32 const required = KillsRequired();
        if (state.kills.size() >= required)
        {
            Dishonor(killer, state, now);
            return;
        }

        CharacterDatabase.Execute("INSERT INTO `mod_pvp_titles_dishonorable_kills` (`guid`, `time`) VALUES ({}, {})", guid, now);
        ChatHandler(killer->GetSession()).PSendSysMessage("Dishonorable kill: {} of {} within {}.",
            state.kills.size(), required, secsToTimeString(Window()));
        PvPTitlesExt::SendDishonorState(killer);
    }
}

void PvPTitlesExt::SendDishonorState(Player* player)
{
    if (!Enabled())
    {
        SendAddonMessage(player, "DISHONOR:0,0,0,0");
        return;
    }

    uint32 const now = Now();
    uint32 left = 0;
    uint32 kills = 0;
    if (DishonorState* state = Find(player))
    {
        Prune(*state, now);
        left = state->expires > now ? state->expires - now : 0;
        kills = uint32(state->kills.size());
    }

    SendAddonMessage(player, Acore::StringFormat("DISHONOR:{},{},{},{}", left, kills, KillsRequired(), Window()));
}

class PvPTitlesDishonorPlayer : public PlayerScript
{
public:
    PvPTitlesDishonorPlayer() : PlayerScript("PvPTitlesDishonorPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_CREATURE_KILL,
        PLAYERHOOK_ON_CREATURE_KILLED_BY_PET,
        PLAYERHOOK_ON_DELETE_FROM_DB
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!Enabled())
            return;

        uint32 const now = Now();
        uint32 const since = now > Window() ? now - Window() : 0;
        ObjectGuid::LowType const guid = player->GetGUID().GetCounter();
        DishonorState state;

        if (QueryResult result = CharacterDatabase.Query("SELECT `expires` FROM `mod_pvp_titles_dishonor` WHERE `guid` = {}", guid))
            state.expires = result->Fetch()[0].Get<uint32>();

        if (state.expires && state.expires <= now)
        {
            state.expires = 0;
            Save(player, state);
        }

        CharacterDatabase.Execute("DELETE FROM `mod_pvp_titles_dishonorable_kills` WHERE `guid` = {} AND `time` <= {}", guid, since);
        if (QueryResult result = CharacterDatabase.Query("SELECT `time` FROM `mod_pvp_titles_dishonorable_kills` WHERE `guid` = {} AND `time` > {} ORDER BY `time`", guid, since))
        {
            do
                state.kills.push_back(result->Fetch()[0].Get<uint32>());
            while (result->NextRow());
        }

        // a debuff saved with the character outlives a state that ended offline
        if (!state.expires)
            UpdateAura(player, 0, now);

        if (!state.expires && state.kills.empty())
            return;

        States[guid] = state;
        if (state.expires)
            ChatHandler(player->GetSession()).PSendSysMessage("You are Dishonored for another {}.", secsToTimeString(state.expires - now));
        // the effects are applied by the first update, in the world
    }

    void OnPlayerLogout(Player* player) override
    {
        auto itr = States.find(player->GetGUID().GetCounter());
        if (itr == States.end())
            return;

        if (itr->second.applied)
            --DishonoredOnline;
        States.erase(itr);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        DishonorState* state = Find(player);
        if (!state)
            return;

        if (state->checkTimer > diff)
        {
            state->checkTimer -= diff;
            return;
        }
        state->checkTimer = CHECK_INTERVAL;

        uint32 const now = Now();
        if (state->expires && state->expires <= now)
        {
            Expire(player, *state);
            return;
        }

        // the debuff: its time set once after login, put back if it went
        // missing; it stays in battlegrounds, where the time still runs
        if (state->expires > now)
        {
            uint32 const spellId = AuraSpell();
            if (spellId && (!state->auraSynced || !player->HasAura(spellId, player->GetGUID())))
            {
                UpdateAura(player, state->expires, now);
                state->auraSynced = true;
            }
        }

        bool const apply = ShouldApply(player, *state, now);
        if (apply != state->applied)
            SetEffects(player, *state, apply);

        // /pvp off would drop the flag after five minutes
        if (state->applied && !player->IsPvP())
            player->UpdatePvP(true, true);
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        if (Enabled() && IsDishonorableVictim(killer, killed))
            OnDishonorableKill(killer);
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        if (Enabled() && IsDishonorableVictim(owner, killed))
            OnDishonorableKill(owner);
    }

    void OnPlayerDeleteFromDB(CharacterDatabaseTransaction trans, uint32 guid) override
    {
        trans->Append("DELETE FROM `mod_pvp_titles_dishonor` WHERE `guid` = {}", guid);
        trans->Append("DELETE FROM `mod_pvp_titles_dishonorable_kills` WHERE `guid` = {}", guid);
    }
};

class PvPTitlesDishonorUnit : public UnitScript
{
public:
    PvPTitlesDishonorUnit() : UnitScript("PvPTitlesDishonorUnit", true, {
        UNITHOOK_IF_NORMAL_REACTION,
        UNITHOOK_ON_PATCH_VALUES_UPDATE
    }) { }

    bool IfNormalReaction(Unit const* unit, Unit const* target, ReputationRank& repRank) override
    {
        if (!DishonoredOnline)
            return true;

        Player const* self = unit->GetAffectingPlayer();
        Player const* other = target->GetAffectingPlayer();
        if (!IsDishonored(self) && !IsDishonored(other))
            return true;

        if (self && other)
        {
            if (self == other || self->IsInRaidWith(other))
                return true;

            repRank = REP_HOSTILE;
            return false;
        }

        // a creature and a Dishonored player: what would be friendly is hostile
        if (IsSpared(self ? target : unit))
            return true;

        if (unit->GetFactionReactionTo(unit->GetFactionTemplateEntry(), target) <= REP_NEUTRAL)
            return true;

        repRank = REP_HOSTILE;
        return false;
    }

    void OnPatchValuesUpdate(Unit const* unit, ByteBuffer& valuesUpdateBuf, BuildValuesCachePosPointers& posPointers, Player* target) override
    {
        if (!DishonoredOnline || !target || posPointers.UnitFieldFactionTemplatePos < 0)
            return;

        Player const* owner = unit->GetAffectingPlayer();
        if (owner == target)
            return;

        bool const targetDishonored = IsDishonored(target);
        if (owner)
        {
            // a player (or what he controls) and a Dishonored one, of the same
            // side and not grouped: each is shown the other as an enemy
            if (!targetDishonored && !IsDishonored(owner))
                return;

            if (owner->IsInRaidWith(target) || owner->GetTeamId() != target->GetTeamId())
                return;

            uint32 const enemy = target->GetTeamId() == TEAM_ALLIANCE ? FACTION_TEMPLATE_PLAYER_ORC : FACTION_TEMPLATE_PLAYER_HUMAN;
            valuesUpdateBuf.put(posPointers.UnitFieldFactionTemplatePos, enemy);
            return;
        }

        // a creature seen by a Dishonored player. Factions with a reputation
        // reach his client as forced reactions; the others, if friendly to
        // him, are shown as monsters
        if (!targetDishonored || IsSpared(unit))
            return;

        if (unit->GetFactionReactionTo(unit->GetFactionTemplateEntry(), target) <= REP_NEUTRAL)
            return;

        valuesUpdateBuf.put(posPointers.UnitFieldFactionTemplatePos, FACTION_TEMPLATE_MONSTER);
    }
};

void AddPvpTitlesDishonorScripts()
{
    new PvPTitlesDishonorPlayer();
    new PvPTitlesDishonorUnit();
}
