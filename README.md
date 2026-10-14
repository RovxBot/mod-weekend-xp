# Weekend Bonus events for AzerothCore

One server-wide bonus runs each weekend. The default schedule is Friday
00:00 through Monday 00:00 in the **worldserver's local timezone**.
The default selection is randomized, and all five events give **+50%**.

| Event | Bonus applies to |
| --- | --- |
| Experience | Normal XP sources, below the core's maximum player level |
| Reputation | Positive earned reputation; losses remain unchanged |
| Money | Looted gold and money received from quest rewards |
| Battleground | Honor from kills, objectives and end-of-match rewards |
| Dungeon Finder | Random Dungeon Finder completion XP and money |

Random mode shuffles every enabled event into a cycle. Each event appears
once per cycle, and adjacent weekends never repeat an event when at least
two events are enabled. The schedule, seed and pool determine the selection;
a restart does not reroll the weekend. Round-robin follows the table order.
Empty pools disable events. Changing the seed, pool or schedule intentionally
recalculates selection.

Percentages are additional rewards: `50` means 1.5x, `100` means 2x.
Existing core rates apply first. Only one weekend event runs at a time.

## Server installation

1. Place this repository under AzerothCore's `modules` directory.
2. Reconfigure and rebuild AzerothCore using its normal module workflow.
3. Import `data/sql/db-world/mod-weekend-bonus.sql` into the world database,
   or use AzerothCore's module database assembler.
4. Copy `conf/mod-weekend-bonus.conf.dist` to `mod-weekend-bonus.conf` in
   the server's configuration directory.
5. Edit the settings and restart worldserver. `.reload config` also applies
   configuration changes; the buff refreshes within five seconds.

Requires current AzerothCore with the hooks used in `src/WeekendBonus.cpp`
and C++20 support. When upgrading, replace the previous module configuration
with the new file. Old `XPWeekend.*` settings, per-player XP overrides and
opt-outs are no longer read. The SQL removes obsolete commands/translations.
No encounter-spell migration or encounter-specific code is included.

## Buff icon and exact percentage

Copy `client/WeekendBonus` into each player's
`Interface/AddOns/WeekendBonus` directory, enable **Weekend Bonus** at
character selection, then log in or reload the UI.

The addon displays the event's icon, a `+50%` label, its exact configured
percentage, reward scope and remaining time on the normal player buff bar.
The marker has no character visual or gameplay effects and is not saved to
the character database. It is removed when the event ends or buff display
is disabled; after death it returns within five seconds of resurrection.

An unmodified 3.3.5 client reads spell names and tooltip numbers from its
own DBC data, so the addon is required for accurate dynamic buff text.
Clients without it still receive gameplay bonuses, login announcements and
chat commands; the module does not give them a misleading stock buff.

The marker uses an unused copy of Bonfire's Blessing (46668), converted to
a cosmetic dummy. The global spell definition is unchanged, and only this
module's marker is removed by its display logic.

## Configuration and commands

All settings are documented in `conf/mod-weekend-bonus.conf.dist`.

- `.weekendbonus status`: current event, percentage and ending time.
- `.weekendbonus calendar`: this active weekend plus upcoming weekends,
  or the next five weekends when no event is active.
- `WeekendBonus.Selection = "random"` or `"round-robin"` selects rotation.
- Each event has `.Enabled` and `.BonusPercent` settings. A 0% event is
  excluded; percentages use two decimal places in the range 0–1000.
- `WeekendBonus.AlwaysActive = 1` bypasses the weekend window for testing.
  `EventOverride` selects an enabled event: 1 XP, 2 reputation, 3 money,
  4 BG, 5 RDF. `WeekendBonus.Enabled` remains the master switch.

Loot money is increased once before the normal party split. Quest money
uses money actually received, respecting reward costs, max-level conversion
and play-time reductions. Trades, mail, auctions, vendor sales, refunds and
bank withdrawals never trigger the bonus. Quest bonus gold is paid separately
after the normal reward and announced in chat; quest/RDF reward previews
retain their original amounts.

BG honor follows the core's earned-honor contribution counter, including
objectives and final rewards. It is settled on player updates, at match
rewards and when leaving the BG before logout. The core honor cap applies;
the scoreboard is updated where available. Arenas and world PvP are excluded.
Bonus honor does not generate extra BG XP; the XP weekend controls that.

RDF rewards apply only to completion rewards with the normal DF quest flags,
including daily and repeat rewards. Dungeon creature XP, ordinary dungeon
quests, item rewards and emblems are unchanged. At maximum level, RDF players
can still receive bonus gold.

## Validation

Run `bash tests/run-tests.sh` for the standalone C++ rotation/reward tests
and addon protocol/display tests. These do not configure or build the core.
A full worldserver build and in-game checks are required before deployment.

## Credits

Based on the original [XP Weekend module](https://github.com/azerothcore/mod-weekend-xp)
by [Talamortis](https://github.com/Talamortis) and AzerothCore contributors.
