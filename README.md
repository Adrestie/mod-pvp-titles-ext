# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore
## mod-pvp-titles-ext
### This is a module for [AzerothCore](http://www.azerothcore.org)

A fork of [azerothcore/mod-pvp-titles](https://github.com/azerothcore/mod-pvp-titles) that keeps its titles and adds:

- an **addon channel**: the server tells client addons the rank thresholds it uses, and the player's Dishonored state;
- the **Dishonored** state: killing too many civilians of the enemy faction turns every faction against the player.

#### Features:
- Vanilla PvP Titles for Azerothcore obtained with Honorable Kills (unchanged)
- Addon channel (`PvPTitles.AddonMessages`)
- Dishonored state (`PvPTitles.Dishonor.*`)

### This module currently requires:
- AzerothCore v1.0.1+

### How to install
1. Place the module under the `modules` folder of your AzerothCore source folder. The folder must be named `mod-pvp-titles-ext` (the script loader is named after it). Remove `mod-pvp-titles` if it is there: both would run.
2. Re-run cmake and launch a clean build of AzerothCore
3. Create the mod_pvptitles.conf based on `mod_pvptitles.conf.dist` in your config folder and enable the module. Existing `mod_pvptitles.conf` files keep working; the new settings fall back to their defaults.
4. The characters database tables (`data/sql/db-characters`) are applied by the database updater.
5. Log in game, kill 50 enemy players and see the result. You can make a macro for that with `.die` and `.revive`

### Addon channel
Clients cannot read the server configuration, so an interface has no way to know how many honorable kills the next rank needs. The server whispers the player itself with `LANG_ADDON`; addons receive `CHAT_MSG_ADDON` with the prefix `PVPTITLES`:

| Message | Meaning |
|---|---|
| `RANKS:<k1>,...,<k14>` | lifetime honorable kills required for ranks 1 to 14 |
| `DISHONOR:<left>,<kills>,<required>,<window>` | seconds left Dishonored (0: not Dishonored), dishonorable kills within the window, kills that make one Dishonored, window in seconds; all zero when the feature is disabled |
| `CIV:<entry>:<0\|1>` | whether the creature template `<entry>` is flagged `CREATURE_FLAG_EXTRA_CIVILIAN` (the 3.3.5 creature query does not carry the flag), 0 for an unknown entry |

`RANKS` is sent at login. An addon asks for both messages by whispering itself `REQ` with the same prefix (the login message may reach the client before its interface is loaded, and `/reload` loses it):

```lua
SendAddonMessage("PVPTITLES", "REQ", "WHISPER", UnitName("player"))
```

The civilian flag is asked the same way, one creature entry at a time -- `CIV:<entry>`, the entry read from the creature's GUID -- and answered with `CIV:<entry>:<0|1>`.

Only accept these messages from the player himself.

### Dishonored
The core no longer tracks dishonorable kills. This brings back the old punishment for killing the non-combatants other players depend on.

- A **dishonorable kill** is the killing blow, by the player or his pet, on a creature flagged `CREATURE_FLAG_EXTRA_CIVILIAN` whose faction belongs to the enemy side. Triggers, critters, totems and player-controlled units never count.
- `PvPTitles.Dishonor.Kills` of them within `PvPTitles.Dishonor.Window` seconds make the player **Dishonored** for `PvPTitles.Dishonor.Duration` seconds of real time (offline time counts). Another dishonorable kill while Dishonored starts the duration again.
- While Dishonored:
  - every faction, his own included, is hostile to him: guards attack him, vendors, flight masters and innkeepers refuse him. Spirit healers and spirit guides are spared;
  - any player outside his group may attack him, and he may fight back. He stays flagged for PvP. Players of his own side see him as an enemy, and he sees them as enemies: the server sends each client an enemy player faction template, the way the core shows mixed-faction group members as friends. No client patch is needed;
  - the effects are suspended in battlegrounds and arenas.
- The player is told of each dishonorable kill, of the state and of its end by system messages.
- **The debuff is drawn by the client's interface.** A client shows an aura only if its own `Spell.dbc` knows the spell, and no spell of the original 3.3.5a client has a fitting name, tooltip and no visual effect. So the server puts no aura by default: it sends the state and the time left through the addon channel (`DISHONOR`), and an addon draws the debuff - [mod-forever-ui](https://github.com/Adrestie/WoW-mods) does. A server that ships a client patch can put its own spell on the player with `PvPTitles.Dishonor.AuraSpell`: the module then sets its duration to the time left, keeps it on, and only ever touches the aura the player carries from himself.

Limits: forced reactions coming from auras on a faction with a reputation are lost when the state ends. Joining a group with a same-side player shows the change once the units are sent again (out of sight and back).

## Credits
* [conan513](https://github.com/conan513): (Author of the module):
* [Dreaxxx](https://github.com/Dreaxxx/mod-pvptitles): (Reworked):
* [Adrestie](https://github.com/Adrestie/mod-pvp-titles-ext): addon channel and Dishonored state

AzerothCore: [repository](https://github.com/azerothcore) - [website](http://azerothcore.org/) - [discord chat community](https://discord.gg/PaqQRkd)
