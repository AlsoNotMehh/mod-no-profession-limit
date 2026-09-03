# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore Module: NoProfessionLimit

[![AzerothCore Module](https://img.shields.io/badge/AzerothCore-Module-red?style=flat-square&logo=github)](https://github.com/azerothcore/azerothcore-wotlk)
[![C++20](https://img.shields.io/badge/Language-C++20-00599C?style=flat-square&logo=c%2B%2B)](https://isocpp.org/)
[![Branch 3.3.5a](https://img.shields.io/badge/Branch-3.3.5a-orange?style=flat-square)](https://github.com/azerothcore/azerothcore-wotlk)
[![License MIT](https://img.shields.io/badge/License-MIT-blue?style=flat-square)](LICENSE)

An advanced profession system for **AzerothCore (WotLK 3.3.5a)** that removes or raises the 2-primary-profession limit (up to all 11 WotLK professions) with optional account-bound progression synchronization.

### 💡 Why this module?
In vanilla WotLK, characters are strictly capped at learning only 2 primary professions. Players must either create multiple profession mules or abandon leveled skills to craft needed consumables and equipment. 

**`NoProfessionLimit`** lifts this restriction cleanly—allowing characters to learn all 11 primary professions, while offering optional account-wide synchronization for skill levels, ranks, and recipes across alts without modifying core database schemas.

---

## 📊 Feature Comparison

| Feature | Stock AzerothCore | NoProfessionLimit |
| :--- | :---: | :---: |
| **Primary Profession Limit** | ❌ Capped at 2 professions | ✅ **Configurable limit up to all 11 WotLK primary professions** |
| **Filtering & Allow / Block Lists** | ❌ All or nothing | ✅ **Granular allow/block lists with English and Spanish aliases** |
| **Account-Bound Sync** | ❌ None | ✅ **Optional sync of profession skill values, ranks, and recipes** |
| **Faction Isolation** | ❌ None | ✅ **Optional same-faction-only sync to keep Horde/Alliance separate** |
| **Secondary Professions** | ❌ Unsynced | ✅ **Optional sync for Cooking, First Aid, and Fishing** |
| **Database Requirements** | ❌ None | ✅ **Zero custom SQL tables; writes directly to core tables** |

---

## ⚙️ Technical Architecture

### 1. Dynamic Primary Profession Cap
Hooks directly into AzerothCore's skill learning logic to intercept and validate primary profession counts:
- Allows expanding the maximum primary profession count from 2 up to 11.
- Supports localized alias parsing (e.g. `alchemy`, `alquimia`, `blacksmithing`, `herreria`) or skill IDs in configuration.
- Enforces strict server-side validation to prevent unauthorized profession learning.

### 2. Optional Account-Wide Profession Replication
When account-bound mode is enabled:
- Replicates learned spell recipes and skill rank progression across characters on the same account.
- Integrates seamlessly with AzerothCore's native `character_skills` and `character_spell` tables.
- Employs silent synchronization to eliminate login freezes and chat spam.

---

## 📋 Configuration Reference (`NoProfessionLimit.conf`)

| Setting | Default | Description |
| :--- | :---: | :--- |
| `NoProfessionLimit.Enable` | `1` | Enables the uncapped profession module. |
| `NoProfessionLimit.MaxPrimaryProfessions` | `11` | Maximum primary professions per character (up to 11). |
| `NoProfessionLimit.Professions.AllowList` | `all` | Allowed primary professions list (`all` or specific names/IDs). |
| `NoProfessionLimit.Professions.BlockList` | `""` | Blocked professions excluded from the allow list. |
| `NoProfessionLimit.Sync.Enable` | `0` | Optional master switch for account-bound profession sync. |
| `NoProfessionLimit.Sync.SameFactionOnly` | `0` | Restricts profession synchronization to same-faction alts. |
| `NoProfessionLimit.Sync.SecondaryProfessions` | `0` | Synchronizes Cooking, First Aid, and Fishing across alts. |

---

## 🛠️ Installation

1. Place the module in `azerothcore-wotlk/modules/`:
   ```bash
   cd azerothcore-wotlk/modules
   git clone https://github.com/AlsoNotMehh/NoProfessionLimit.git
   ```
2. Re-run CMake and compile your server:
   ```bash
   cmake -B build
   cmake --build build --config Release
   ```
3. Copy `conf/NoProfessionLimit.conf.dist` to your `worldserver` configs directory as `NoProfessionLimit.conf` and customize as needed.

---

## 🤝 Credits

- **Author & Enhancements:** [AlsoNotMehh](https://github.com/AlsoNotMehh)
- **Framework:** [AzerothCore](https://www.azerothcore.org)

---

## 📜 License

This project is licensed under the [MIT License](LICENSE).
