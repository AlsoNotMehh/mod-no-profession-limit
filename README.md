# NoProfessionLimit

NoProfessionLimit is an AzerothCore module for WotLK 3.3.5a that raises or
removes the default two-primary-profession limit.

It can optionally make professions account-bound, allowing profession ranks,
skill progress and recipes to be shared between eligible characters on the same
account without adding custom database tables.

## Features

- Allow characters to learn up to all 11 WotLK primary professions.
- Configure which primary professions are allowed or blocked.
- Optional allow-list enforcement for public realms.
- Optional account-bound profession sync.
- Optional same-faction-only sync to keep Horde and Alliance progress separate.
- Optional secondary profession sync for Cooking, First Aid and Fishing.
- Sync profession ranks, skill values, max skill values and recipes.
- Uses AzerothCore's existing `character_skills` and `character_spell` tables.
- No custom SQL tables are required.

## Compatibility

- AzerothCore WotLK 3.3.5a
- Built as a normal source module under `modules/`
- Tested with a Release worldserver build on Windows

## Installation

1. Copy the module folder into your AzerothCore source tree:

```text
azerothcore-wotlk/modules/NoProfessionLimit
```

2. Re-run CMake for your AzerothCore build.

3. Build `worldserver`.

4. Copy the config file:

```text
modules/NoProfessionLimit/conf/NoProfessionLimit.conf.dist
```

to your server config module directory as:

```text
configs/modules/NoProfessionLimit.conf
```

5. Restart `worldserver`.

On startup the module logs its current status, including the configured maximum
primary professions and whether account-bound sync is enabled.

## Quick Config

The default config lets each character learn every WotLK primary profession:

```ini
NoProfessionLimit.Enable = 1
NoProfessionLimit.MaxPrimaryProfessions = 11
NoProfessionLimit.Professions.AllowList = all
NoProfessionLimit.Professions.BlockList = ""
```

Use `MaxPrimaryProfessions = 0` to automatically use the number of professions
enabled in `AllowList`.

## Profession Allow List

`NoProfessionLimit.Professions.AllowList` accepts names, Spanish aliases or
skill IDs.

Accepted English names:

```text
alchemy, blacksmithing, enchanting, engineering, herbalism,
inscription, jewelcrafting, leatherworking, mining, skinning, tailoring
```

Accepted Spanish aliases:

```text
alquimia, herreria, encantamiento, ingenieria, herboristeria,
inscripcion, joyeria, peleteria, mineria, desuello, sastreria
```

Skill IDs:

```text
171,164,333,202,182,773,755,165,186,393,197
```

Example:

```ini
NoProfessionLimit.Professions.AllowList = alchemy, enchanting, tailoring
NoProfessionLimit.MaxPrimaryProfessions = 3
```

You can also start from `all` and remove specific professions:

```ini
NoProfessionLimit.Professions.AllowList = all
NoProfessionLimit.Professions.BlockList = engineering, inscription
```

## Enforcing The Allow List

By default, the module counts only allowed professions but does not remove
disallowed professions from existing characters.

For strict public realm rules you can enable:

```ini
NoProfessionLimit.Professions.EnforceAllowedList = 1
```

Warning: this can remove blocked primary professions from characters on login
or immediately after learning. Keep it disabled unless that is intentional.

## Account-Bound Professions

Account-bound sync is disabled by default for public-realm safety:

```ini
NoProfessionLimit.AccountBound.Enable = 0
```

To enable it:

```ini
NoProfessionLimit.AccountBound.Enable = 1
NoProfessionLimit.AccountBound.SameFactionOnly = 1
NoProfessionLimit.AccountBound.SyncSkillProgress = 1
NoProfessionLimit.AccountBound.SyncProfessionRanks = 1
NoProfessionLimit.AccountBound.SyncRecipes = 1
```

With this setup, same-faction characters on the same account can share:

- Profession rank spells such as Apprentice through Grand Master.
- Current skill value and max skill value.
- Learned profession recipes stored in `character_spell`.

`SameFactionOnly = 1` is recommended for public realms. It prevents Horde
characters from giving profession progress or recipes to Alliance characters on
the same account, and the other way around.

## Secondary Professions

Secondary professions are disabled in account-bound sync by default:

```ini
NoProfessionLimit.AccountBound.IncludeSecondaryProfessions = 0
```

Enable this if you want Cooking, First Aid and Fishing to sync too:

```ini
NoProfessionLimit.AccountBound.IncludeSecondaryProfessions = 1
```

Secondary professions do not count against `MaxPrimaryProfessions`.

## Recipe Sync Rules

Recipe sync copies learned profession spells that belong to managed profession
skill lines.

Recommended setting:

```ini
NoProfessionLimit.AccountBound.RequireRecipeSkill = 1
```

When enabled, a target character must have enough profession skill before a
recipe is learned. If `SyncSkillProgress` is enabled, skill progress is applied
before recipes on login, so normal account-bound recipe sync still works.

For custom recipes, use:

```ini
NoProfessionLimit.AccountBound.SpellAllowList = 900001,900002
```

To keep special spells or profession specializations character-specific:

```ini
NoProfessionLimit.AccountBound.SpellBlockList = 20219,20222
```

## Startup Backfill

For existing realms, this option performs a broad database migration at startup:

```ini
NoProfessionLimit.AccountBound.StartupBackfill = 1
```

It pushes already-known account profession progress into eligible characters on
the same account.

Use it carefully on large realms because it can touch many rows. A common
migration flow is:

1. Enable account-bound sync.
2. Enable `StartupBackfill`.
3. Start the worldserver once and let the backfill finish.
4. Stop the worldserver.
5. Set `StartupBackfill = 0`.
6. Start the worldserver normally.

## Notes For Public Realms

Recommended safe defaults:

```ini
NoProfessionLimit.AccountBound.Enable = 0
NoProfessionLimit.AccountBound.SameFactionOnly = 1
NoProfessionLimit.AccountBound.StartupBackfill = 0
NoProfessionLimit.Professions.EnforceAllowedList = 0
```

If you enable account-bound sync publicly, keep `SameFactionOnly = 1` unless
your realm intentionally allows cross-faction progression sharing.

Online target characters may need to relog before DB-written sync changes are
visible in the client.

## Troubleshooting

If characters still only have two profession slots:

- Confirm the module was compiled into `worldserver`.
- Confirm `NoProfessionLimit.Enable = 1`.
- Confirm the active config is in `configs/modules/NoProfessionLimit.conf`.
- Restart `worldserver` after changing the config.

If account-bound recipes do not appear:

- Confirm `NoProfessionLimit.AccountBound.Enable = 1`.
- Confirm `NoProfessionLimit.AccountBound.SyncRecipes = 1`.
- Confirm both characters are eligible under `SameFactionOnly`.
- Confirm the target has enough skill if `RequireRecipeSkill = 1`.
- Relog the target character.

If blocked professions are still present:

- `BlockList` alone only removes them from the module's allowed set.
- Enable `NoProfessionLimit.Professions.EnforceAllowedList = 1` only if you
  want the module to remove blocked primary professions from characters.

## Files

```text
NoProfessionLimit/
  conf/
    NoProfessionLimit.conf.dist
  src/
    NoProfessionLimit.cpp
  README.md
```

## License

This module is intended for AzerothCore and is distributed under GPL-2.0 to
match the AzerothCore core license.
