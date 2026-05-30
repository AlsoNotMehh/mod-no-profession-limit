/*
 * NoProfessionLimit module for AzerothCore.
 */

#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCEnums.h"
#include "DBCStores.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Tokenize.h"
#include "World.h"
#include "WorldConfig.h"
#include "WorldScript.h"
#include "WorldSessionMgr.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
constexpr uint32 MaxSupportedPrimaryProfessions = 11;
constexpr uint16 DefaultMaxProfessionSkill = 450;

struct ProfessionDefinition
{
    uint32 SkillId;
    std::string_view Name;
    std::array<uint32, 6> RankSpells;
};

constexpr std::array<ProfessionDefinition, MaxSupportedPrimaryProfessions> PrimaryProfessionDefinitions =
{{
    { SKILL_ALCHEMY,       "alchemy",       { 2259, 3101, 3464, 11611, 28596, 51304 } },
    { SKILL_BLACKSMITHING, "blacksmithing", { 2018, 3100, 3538, 9785, 29844, 51300 } },
    { SKILL_ENCHANTING,   "enchanting",    { 7411, 7412, 7413, 13920, 28029, 51313 } },
    { SKILL_ENGINEERING,  "engineering",   { 4036, 4037, 4038, 12656, 30350, 51306 } },
    { SKILL_HERBALISM,    "herbalism",     { 2366, 2368, 3570, 11993, 28695, 50300 } },
    { SKILL_INSCRIPTION,  "inscription",   { 45357, 45358, 45359, 45360, 45361, 45363 } },
    { SKILL_JEWELCRAFTING,"jewelcrafting", { 25229, 25230, 28894, 28895, 28897, 51311 } },
    { SKILL_LEATHERWORKING,"leatherworking",{ 2108, 3104, 3811, 10662, 32549, 51302 } },
    { SKILL_MINING,       "mining",        { 2575, 2576, 3564, 10248, 29354, 50310 } },
    { SKILL_SKINNING,     "skinning",      { 8613, 8617, 8618, 10768, 32678, 50305 } },
    { SKILL_TAILORING,    "tailoring",     { 3908, 3909, 3910, 12180, 26790, 51309 } },
}};

constexpr std::array<ProfessionDefinition, 3> SecondaryProfessionDefinitions =
{{
    { SKILL_COOKING,   "cooking",  { 2550, 3102, 3413, 18260, 33359, 51296 } },
    { SKILL_FIRST_AID, "firstaid", { 3273, 3274, 7924, 10846, 27028, 45542 } },
    { SKILL_FISHING,   "fishing",  { 7620, 7731, 7732, 18248, 33095, 51294 } },
}};

struct CharacterInfo
{
    uint32 Guid = 0;
    uint32 AccountId = 0;
    uint8 Race = 0;
    uint8 Class = 0;
};

struct SkillSnapshot
{
    uint16 Value = 0;
    uint16 Max = 0;
};

struct ModuleConfig
{
    bool Enabled = true;
    uint32 MaxPrimaryProfessions = MaxSupportedPrimaryProfessions;
    bool CountOnlyAllowedPrimaryProfessions = true;
    bool EnforceAllowedPrimaryProfessions = false;
    uint16 MaxSyncedSkillValue = DefaultMaxProfessionSkill;

    bool AccountBoundEnabled = false;
    bool AccountBoundSameFactionOnly = true;
    bool AccountBoundStartupBackfill = false;
    bool AccountBoundSyncOnLogin = true;
    bool AccountBoundSyncOnLearn = true;
    bool AccountBoundSyncOnlineOnReload = true;
    bool AccountBoundIncludeSecondaryProfessions = false;
    bool AccountBoundSyncSkillProgress = true;
    bool AccountBoundSyncProfessionRanks = true;
    bool AccountBoundSyncRecipes = true;
    bool AccountBoundSyncSkillGrantedSpells = true;
    bool AccountBoundRequireRecipeSkill = true;

    std::unordered_set<uint32> AllowedPrimarySkills;
    std::unordered_set<uint32> SpellAllowList;
    std::unordered_set<uint32> SpellBlockList;
};

ModuleConfig Config;
std::unordered_set<uint32> ApplyingAccountSyncGuids;

std::string NormalizeToken(std::string_view token)
{
    std::string normalized;
    normalized.reserve(token.size());

    for (char c : token)
    {
        if (c == ' ' || c == '_' || c == '-' || c == '.')
            continue;

        normalized.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    }

    return normalized;
}

uint32 ResolveProfessionSkillToken(std::string_view rawToken)
{
    std::string const token = NormalizeToken(rawToken);

    if (Optional<uint32> id = Acore::StringTo<uint32>(rawToken))
        return *id;

    static std::unordered_map<std::string, uint32> const SkillNames =
    {
        { "alchemy", SKILL_ALCHEMY },
        { "alquimia", SKILL_ALCHEMY },
        { "blacksmithing", SKILL_BLACKSMITHING },
        { "smithing", SKILL_BLACKSMITHING },
        { "herreria", SKILL_BLACKSMITHING },
        { "enchanting", SKILL_ENCHANTING },
        { "enchant", SKILL_ENCHANTING },
        { "encantamiento", SKILL_ENCHANTING },
        { "engineering", SKILL_ENGINEERING },
        { "ingenieria", SKILL_ENGINEERING },
        { "herbalism", SKILL_HERBALISM },
        { "herbs", SKILL_HERBALISM },
        { "herboristeria", SKILL_HERBALISM },
        { "inscription", SKILL_INSCRIPTION },
        { "inscripcion", SKILL_INSCRIPTION },
        { "jewelcrafting", SKILL_JEWELCRAFTING },
        { "jewel", SKILL_JEWELCRAFTING },
        { "joyeria", SKILL_JEWELCRAFTING },
        { "leatherworking", SKILL_LEATHERWORKING },
        { "leather", SKILL_LEATHERWORKING },
        { "peleteria", SKILL_LEATHERWORKING },
        { "mining", SKILL_MINING },
        { "mineria", SKILL_MINING },
        { "skinning", SKILL_SKINNING },
        { "desuello", SKILL_SKINNING },
        { "tailoring", SKILL_TAILORING },
        { "sastreria", SKILL_TAILORING },
        { "cooking", SKILL_COOKING },
        { "cocina", SKILL_COOKING },
        { "firstaid", SKILL_FIRST_AID },
        { "primerosauxilios", SKILL_FIRST_AID },
        { "fishing", SKILL_FISHING },
        { "pesca", SKILL_FISHING },
    };

    auto itr = SkillNames.find(token);
    return itr != SkillNames.end() ? itr->second : 0;
}

bool IsSupportedPrimarySkill(uint32 skillId)
{
    return std::any_of(PrimaryProfessionDefinitions.begin(), PrimaryProfessionDefinitions.end(),
        [skillId](ProfessionDefinition const& profession)
        {
            return profession.SkillId == skillId;
        });
}

bool IsSupportedSecondarySkill(uint32 skillId)
{
    return std::any_of(SecondaryProfessionDefinitions.begin(), SecondaryProfessionDefinitions.end(),
        [skillId](ProfessionDefinition const& profession)
        {
            return profession.SkillId == skillId;
        });
}

ProfessionDefinition const* GetProfessionDefinition(uint32 skillId)
{
    for (ProfessionDefinition const& profession : PrimaryProfessionDefinitions)
        if (profession.SkillId == skillId)
            return &profession;

    for (ProfessionDefinition const& profession : SecondaryProfessionDefinitions)
        if (profession.SkillId == skillId)
            return &profession;

    return nullptr;
}

std::unordered_set<uint32> ParsePrimaryProfessionList(std::string const& rawList, bool defaultToAll)
{
    std::unordered_set<uint32> skills;
    bool sawToken = false;

    for (std::string_view rawToken : Acore::Tokenize(rawList, ',', false))
    {
        std::string const token = NormalizeToken(rawToken);
        if (token.empty())
            continue;

        sawToken = true;

        if (token == "all")
        {
            for (ProfessionDefinition const& profession : PrimaryProfessionDefinitions)
                skills.insert(profession.SkillId);
            continue;
        }

        if (token == "none")
        {
            skills.clear();
            continue;
        }

        uint32 const skillId = ResolveProfessionSkillToken(rawToken);
        if (IsSupportedPrimarySkill(skillId))
            skills.insert(skillId);
        else
            LOG_WARN("module.noprofessionlimit", "NoProfessionLimit: ignoring unknown primary profession token '{}'.", std::string(rawToken));
    }

    if ((!sawToken || skills.empty()) && defaultToAll)
        for (ProfessionDefinition const& profession : PrimaryProfessionDefinitions)
            skills.insert(profession.SkillId);

    return skills;
}

std::unordered_set<uint32> ParseSpellList(std::string const& rawList)
{
    std::unordered_set<uint32> spells;

    for (std::string_view rawToken : Acore::Tokenize(rawList, ',', false))
    {
        std::string const token = NormalizeToken(rawToken);
        if (token.empty())
            continue;

        if (Optional<uint32> spellId = Acore::StringTo<uint32>(rawToken))
            spells.insert(*spellId);
        else
            LOG_WARN("module.noprofessionlimit", "NoProfessionLimit: ignoring unknown spell id token '{}'.", std::string(rawToken));
    }

    return spells;
}

bool IsAllowedPrimarySkill(uint32 skillId)
{
    return Config.AllowedPrimarySkills.contains(skillId);
}

bool IsManagedProfessionSkill(uint32 skillId)
{
    return IsAllowedPrimarySkill(skillId) || (Config.AccountBoundIncludeSecondaryProfessions && IsSupportedSecondarySkill(skillId));
}

uint32 RaceMaskForRace(uint8 race)
{
    return race ? 1u << (race - 1) : 0;
}

uint32 ClassMaskForClass(uint8 playerClass)
{
    return playerClass ? 1u << (playerClass - 1) : 0;
}

bool CanShareBetweenRaces(uint8 sourceRace, uint8 targetRace)
{
    return !Config.AccountBoundSameFactionOnly || Player::TeamIdForRace(sourceRace) == Player::TeamIdForRace(targetRace);
}

uint16 ClampSyncedSkillValue(uint32 value)
{
    return uint16(std::min<uint32>(value, Config.MaxSyncedSkillValue));
}

uint16 GetSkillStepForMaxValue(uint32 skillId, uint16 maxValue, uint8 race, uint8 playerClass)
{
    SkillRaceClassInfoEntry const* raceClassInfo = GetSkillRaceClassInfo(skillId, race, playerClass);
    if (!raceClassInfo)
        return 0;

    SkillTiersEntry const* skillTier = sSkillTiersStore.LookupEntry(raceClassInfo->SkillTierID);
    if (!skillTier)
        return 0;

    uint16 bestStep = 0;
    for (uint32 i = 0; i < MAX_SKILL_STEP; ++i)
    {
        if (skillTier->Value[i] == maxValue)
            return uint16(i + 1);

        if (skillTier->Value[i] && skillTier->Value[i] < maxValue)
            bestStep = uint16(i + 1);
    }

    return bestStep;
}

uint32 GetProfessionSkillFromRankSpell(uint32 spellId, bool managedOnly = true)
{
    if (SpellLearnSkillNode const* learnSkill = sSpellMgr->GetSpellLearnSkill(spellId))
        if (!managedOnly || IsManagedProfessionSkill(learnSkill->skill))
            return learnSkill->skill;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return 0;

    for (SpellEffectInfo const& effect : spellInfo->Effects)
        if (effect.Effect == SPELL_EFFECT_SKILL && (!managedOnly || IsManagedProfessionSkill(effect.MiscValue)))
            return effect.MiscValue;

    return 0;
}

uint32 GetBestRankSpellForSkill(uint32 skillId, uint16 maxValue)
{
    ProfessionDefinition const* profession = GetProfessionDefinition(skillId);
    if (!profession)
        return 0;

    uint32 rankIndex = 0;
    if (maxValue > 375)
        rankIndex = 5;
    else if (maxValue > 300)
        rankIndex = 4;
    else if (maxValue > 225)
        rankIndex = 3;
    else if (maxValue > 150)
        rankIndex = 2;
    else if (maxValue > 75)
        rankIndex = 1;

    return profession->RankSpells[rankIndex];
}

bool IsSpellAllowedForTargetCharacter(uint32 spellId, CharacterInfo const& target)
{
    bool sawManagedAbility = false;
    uint32 const targetRaceMask = RaceMaskForRace(target.Race);
    uint32 const targetClassMask = ClassMaskForClass(target.Class);

    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
    {
        SkillLineAbilityEntry const* ability = itr->second;
        if (!ability || !IsManagedProfessionSkill(ability->SkillLine))
            continue;

        sawManagedAbility = true;

        if (ability->RaceMask && !(ability->RaceMask & targetRaceMask))
            continue;

        if (ability->ClassMask && !(ability->ClassMask & targetClassMask))
            continue;

        return true;
    }

    return !sawManagedAbility;
}

bool IsAccountBoundProfessionSpell(uint32 spellId)
{
    if (!sSpellMgr->GetSpellInfo(spellId) || Config.SpellBlockList.contains(spellId))
        return false;

    if (Config.SpellAllowList.contains(spellId))
        return true;

    if (GetProfessionSkillFromRankSpell(spellId))
        return Config.AccountBoundSyncProfessionRanks;

    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
    {
        SkillLineAbilityEntry const* ability = itr->second;
        if (!ability || !IsManagedProfessionSkill(ability->SkillLine))
            continue;

        if (ability->AcquireMethod == SKILL_LINE_ABILITY_LEARNED_ON_SKILL_LEARN)
            return Config.AccountBoundSyncSkillGrantedSpells;

        return Config.AccountBoundSyncRecipes;
    }

    return false;
}

bool PlayerMeetsRecipeSkillRequirement(Player* player, uint32 spellId)
{
    if (!Config.AccountBoundRequireRecipeSkill || GetProfessionSkillFromRankSpell(spellId))
        return true;

    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    bool sawManagedAbility = false;

    for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
    {
        SkillLineAbilityEntry const* ability = itr->second;
        if (!ability || !IsManagedProfessionSkill(ability->SkillLine))
            continue;

        sawManagedAbility = true;
        if (player->GetPureSkillValue(ability->SkillLine) >= ability->MinSkillLineRank)
            return true;
    }

    return !sawManagedAbility;
}

bool CanPlayerLearnSyncedSpell(Player* player, uint32 spellId)
{
    if (!player || player->HasSpell(spellId) || !IsAccountBoundProfessionSpell(spellId))
        return false;

    if (!player->CheckSkillLearnedBySpell(spellId))
        return false;

    CharacterInfo target{ player->GetGUID().GetCounter(), player->GetSession()->GetAccountId(), player->getRace(true), player->getClass() };
    return IsSpellAllowedForTargetCharacter(spellId, target) && PlayerMeetsRecipeSkillRequirement(player, spellId);
}

std::string BuildManagedSkillSqlList()
{
    std::vector<uint32> skillIds;
    skillIds.reserve(Config.AllowedPrimarySkills.size() + SecondaryProfessionDefinitions.size());

    for (uint32 skillId : Config.AllowedPrimarySkills)
        skillIds.push_back(skillId);

    if (Config.AccountBoundIncludeSecondaryProfessions)
        for (ProfessionDefinition const& profession : SecondaryProfessionDefinitions)
            skillIds.push_back(profession.SkillId);

    std::sort(skillIds.begin(), skillIds.end());
    skillIds.erase(std::unique(skillIds.begin(), skillIds.end()), skillIds.end());

    if (skillIds.empty())
        return "0";

    std::string result;
    for (uint32 skillId : skillIds)
    {
        if (!result.empty())
            result += ",";

        result += std::to_string(skillId);
    }

    return result;
}

std::vector<CharacterInfo> LoadAccountCharacters(uint32 accountId)
{
    std::vector<CharacterInfo> characters;

    QueryResult result = CharacterDatabase.Query(
        "SELECT guid, account, race, class FROM characters WHERE account = {}", accountId);

    if (!result)
        return characters;

    do
    {
        Field* fields = result->Fetch();
        characters.push_back({ fields[0].Get<uint32>(), fields[1].Get<uint32>(), fields[2].Get<uint8>(), fields[3].Get<uint8>() });
    } while (result->NextRow());

    return characters;
}

uint32 GetConfiguredMaxPrimaryProfessions()
{
    uint32 maxPrimaryProfessions = Config.MaxPrimaryProfessions;
    uint32 const allowedCount = uint32(Config.AllowedPrimarySkills.size());

    if (!maxPrimaryProfessions)
        maxPrimaryProfessions = allowedCount ? allowedCount : MaxSupportedPrimaryProfessions;

    maxPrimaryProfessions = std::clamp<uint32>(maxPrimaryProfessions, 1, MaxSupportedPrimaryProfessions);

    if (Config.CountOnlyAllowedPrimaryProfessions && allowedCount)
        maxPrimaryProfessions = std::min(maxPrimaryProfessions, allowedCount);

    return maxPrimaryProfessions;
}

uint32 CountPrimaryProfessions(Player const* player)
{
    uint32 count = 0;

    for (ProfessionDefinition const& profession : PrimaryProfessionDefinitions)
    {
        if (!player->HasSkill(profession.SkillId))
            continue;

        if (Config.CountOnlyAllowedPrimaryProfessions && !IsAllowedPrimarySkill(profession.SkillId))
            continue;

        ++count;
    }

    return count;
}

void NormalizeFreeProfessionPoints(Player* player)
{
    if (!Config.Enabled || !player)
        return;

    uint32 const maxPrimaryProfessions = GetConfiguredMaxPrimaryProfessions();
    uint32 const learnedPrimaryProfessions = CountPrimaryProfessions(player);
    uint32 const freeProfessionPoints = learnedPrimaryProfessions >= maxPrimaryProfessions ? 0 : maxPrimaryProfessions - learnedPrimaryProfessions;

    if (player->GetFreePrimaryProfessionPoints() != freeProfessionPoints)
        player->SetFreePrimaryProfessions(uint16(freeProfessionPoints));
}

void EnforceAllowedProfessions(Player* player)
{
    if (!Config.Enabled || !Config.EnforceAllowedPrimaryProfessions || !player)
        return;

    for (ProfessionDefinition const& profession : PrimaryProfessionDefinitions)
    {
        if (IsAllowedPrimarySkill(profession.SkillId) || !player->HasSkill(profession.SkillId))
            continue;

        player->SetSkill(profession.SkillId, 0, 0, 0);
        LOG_INFO("module.noprofessionlimit", "NoProfessionLimit: removed disallowed profession skill {} from player {}.",
            profession.SkillId, player->GetGUID().GetCounter());
    }
}

void ApplyWorldProfessionLimit()
{
    if (!Config.Enabled)
        return;

    sWorld->setIntConfig(CONFIG_MAX_PRIMARY_TRADE_SKILL, GetConfiguredMaxPrimaryProfessions());
}

void AppendOrCommit(CharacterDatabaseTransaction& trans, std::string_view sql)
{
    if (!trans)
        trans = CharacterDatabase.BeginTransaction();

    trans->Append(sql);

    if (trans->GetSize() >= 1000)
    {
        CharacterDatabase.DirectCommitTransaction(trans);
        trans = CharacterDatabase.BeginTransaction();
    }
}

void CommitIfNeeded(CharacterDatabaseTransaction& trans)
{
    if (trans && trans->GetSize())
        CharacterDatabase.DirectCommitTransaction(trans);
}

void InsertOrUpdateSkillForCharacter(CharacterDatabaseTransaction& trans, uint32 targetGuid, uint32 skillId, uint16 value, uint16 max)
{
    if (!value || !max)
        return;

    AppendOrCommit(trans, Acore::StringFormat(
        "INSERT INTO character_skills (guid, skill, value, max) VALUES ({}, {}, {}, {}) "
        "ON DUPLICATE KEY UPDATE value = GREATEST(value, VALUES(value)), max = GREATEST(max, VALUES(max))",
        targetGuid, skillId, value, max));
}

void InsertSpellForCharacter(CharacterDatabaseTransaction& trans, uint32 targetGuid, uint32 spellId)
{
    AppendOrCommit(trans, Acore::StringFormat(
        "INSERT IGNORE INTO character_spell (guid, spell, specMask) VALUES ({}, {}, {})",
        targetGuid, spellId, uint32(SPEC_MASK_ALL)));
}

std::unordered_map<uint32, SkillSnapshot> LoadAccountProfessionSkills(Player* player)
{
    std::unordered_map<uint32, SkillSnapshot> snapshots;

    if (!player || !Config.AccountBoundSyncSkillProgress)
        return snapshots;

    std::string const skillSqlList = BuildManagedSkillSqlList();
    QueryResult result = CharacterDatabase.Query(
        "SELECT c.race, cs.skill, cs.value, cs.max "
        "FROM character_skills cs "
        "INNER JOIN characters c ON c.guid = cs.guid "
        "WHERE c.account = {} AND cs.skill IN ({})",
        player->GetSession()->GetAccountId(), skillSqlList);

    if (!result)
        return snapshots;

    uint8 const targetRace = player->getRace(true);

    do
    {
        Field* fields = result->Fetch();
        uint8 const sourceRace = fields[0].Get<uint8>();
        uint32 const skillId = fields[1].Get<uint16>();
        uint16 value = ClampSyncedSkillValue(fields[2].Get<uint16>());
        uint16 max = ClampSyncedSkillValue(fields[3].Get<uint16>());

        if (!IsManagedProfessionSkill(skillId) || !CanShareBetweenRaces(sourceRace, targetRace))
            continue;

        if (max < value)
            max = value;

        SkillSnapshot& snapshot = snapshots[skillId];
        snapshot.Value = std::max(snapshot.Value, value);
        snapshot.Max = std::max(snapshot.Max, max);
    } while (result->NextRow());

    return snapshots;
}

void LoadAccountProfessionSpells(Player* player, std::vector<uint32>& rankSpells, std::vector<uint32>& recipeSpells)
{
    if (!player)
        return;

    QueryResult result = CharacterDatabase.Query(
        "SELECT DISTINCT c.race, c.class, cs.spell "
        "FROM character_spell cs "
        "INNER JOIN characters c ON c.guid = cs.guid "
        "WHERE c.account = {}",
        player->GetSession()->GetAccountId());

    if (!result)
        return;

    uint8 const targetRace = player->getRace(true);
    std::unordered_set<uint32> seenRankSpells;
    std::unordered_set<uint32> seenRecipeSpells;

    do
    {
        Field* fields = result->Fetch();
        uint8 const sourceRace = fields[0].Get<uint8>();
        uint32 const spellId = fields[2].Get<uint32>();

        if (!CanShareBetweenRaces(sourceRace, targetRace) || !IsAccountBoundProfessionSpell(spellId))
            continue;

        if (GetProfessionSkillFromRankSpell(spellId))
        {
            if (seenRankSpells.insert(spellId).second)
                rankSpells.push_back(spellId);
        }
        else if (seenRecipeSpells.insert(spellId).second)
            recipeSpells.push_back(spellId);
    } while (result->NextRow());
}

bool ApplySkillSnapshot(Player* player, uint32 skillId, SkillSnapshot const& snapshot)
{
    if (!player || !snapshot.Value || !snapshot.Max || !IsManagedProfessionSkill(skillId))
        return false;

    if (!GetSkillRaceClassInfo(skillId, player->getRace(true), player->getClass()))
        return false;

    uint16 value = std::max(player->GetPureSkillValue(skillId), snapshot.Value);
    uint16 max = std::max(player->GetPureMaxSkillValue(skillId), snapshot.Max);
    value = ClampSyncedSkillValue(value);
    max = ClampSyncedSkillValue(max);

    if (max < value)
        max = value;

    if (player->GetPureSkillValue(skillId) >= value && player->GetPureMaxSkillValue(skillId) >= max)
        return false;

    if (Config.AccountBoundSyncProfessionRanks)
        if (uint32 rankSpell = GetBestRankSpellForSkill(skillId, max))
            if (CanPlayerLearnSyncedSpell(player, rankSpell))
                player->learnSpell(rankSpell);

    player->SetSkill(skillId, GetSkillStepForMaxValue(skillId, max, player->getRace(true), player->getClass()), value, max);
    return true;
}

void ApplyAccountSyncToPlayer(Player* player)
{
    if (!Config.Enabled || !Config.AccountBoundEnabled || !Config.AccountBoundSyncOnLogin || !player)
        return;

    uint32 const playerGuid = player->GetGUID().GetCounter();
    ApplyingAccountSyncGuids.insert(playerGuid);

    uint32 learnedRanks = 0;
    uint32 updatedSkills = 0;
    uint32 learnedRecipes = 0;

    std::vector<uint32> rankSpells;
    std::vector<uint32> recipeSpells;
    LoadAccountProfessionSpells(player, rankSpells, recipeSpells);

    for (uint32 spellId : rankSpells)
    {
        if (!CanPlayerLearnSyncedSpell(player, spellId))
            continue;

        player->learnSpell(spellId);
        ++learnedRanks;
    }

    for (auto const& [skillId, snapshot] : LoadAccountProfessionSkills(player))
        if (ApplySkillSnapshot(player, skillId, snapshot))
            ++updatedSkills;

    for (uint32 spellId : recipeSpells)
    {
        if (!CanPlayerLearnSyncedSpell(player, spellId))
            continue;

        player->learnSpell(spellId);
        ++learnedRecipes;
    }

    ApplyingAccountSyncGuids.erase(playerGuid);

    if (learnedRanks || updatedSkills || learnedRecipes)
        LOG_INFO("module.noprofessionlimit", "NoProfessionLimit: account sync applied to player {} (ranks={}, skills={}, recipes={}).",
            playerGuid, learnedRanks, updatedSkills, learnedRecipes);
}

void SyncSkillToAccount(Player* player, uint32 skillId)
{
    if (!Config.Enabled || !Config.AccountBoundEnabled || !Config.AccountBoundSyncOnLearn || !Config.AccountBoundSyncSkillProgress || !player)
        return;

    if (ApplyingAccountSyncGuids.contains(player->GetGUID().GetCounter()) || !IsManagedProfessionSkill(skillId))
        return;

    uint16 value = ClampSyncedSkillValue(player->GetPureSkillValue(skillId));
    uint16 max = ClampSyncedSkillValue(player->GetPureMaxSkillValue(skillId));
    if (!value || !max)
        return;

    if (max < value)
        max = value;

    std::vector<CharacterInfo> characters = LoadAccountCharacters(player->GetSession()->GetAccountId());
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    uint32 synced = 0;
    uint32 const sourceGuid = player->GetGUID().GetCounter();
    uint8 const sourceRace = player->getRace(true);

    for (CharacterInfo const& target : characters)
    {
        if (target.Guid == sourceGuid || !CanShareBetweenRaces(sourceRace, target.Race))
            continue;

        if (!GetSkillRaceClassInfo(skillId, target.Race, target.Class))
            continue;

        InsertOrUpdateSkillForCharacter(trans, target.Guid, skillId, value, max);

        if (Config.AccountBoundSyncProfessionRanks)
            if (uint32 rankSpell = GetBestRankSpellForSkill(skillId, max))
                if (!Config.SpellBlockList.contains(rankSpell) && IsSpellAllowedForTargetCharacter(rankSpell, target))
                    InsertSpellForCharacter(trans, target.Guid, rankSpell);

        ++synced;
    }

    CommitIfNeeded(trans);

    if (synced)
        LOG_DEBUG("module.noprofessionlimit", "NoProfessionLimit: synced skill {} from player {} to {} account character(s).",
            skillId, sourceGuid, synced);
}

void SyncSpellToAccount(Player* player, uint32 spellId)
{
    if (!Config.Enabled || !Config.AccountBoundEnabled || !Config.AccountBoundSyncOnLearn || !player)
        return;

    if (ApplyingAccountSyncGuids.contains(player->GetGUID().GetCounter()) || !IsAccountBoundProfessionSpell(spellId))
        return;

    std::vector<CharacterInfo> characters = LoadAccountCharacters(player->GetSession()->GetAccountId());
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    uint32 synced = 0;
    uint32 const sourceGuid = player->GetGUID().GetCounter();
    uint8 const sourceRace = player->getRace(true);

    for (CharacterInfo const& target : characters)
    {
        if (target.Guid == sourceGuid || !CanShareBetweenRaces(sourceRace, target.Race))
            continue;

        if (!IsSpellAllowedForTargetCharacter(spellId, target))
            continue;

        InsertSpellForCharacter(trans, target.Guid, spellId);
        ++synced;
    }

    CommitIfNeeded(trans);

    if (synced)
        LOG_DEBUG("module.noprofessionlimit", "NoProfessionLimit: synced profession spell {} from player {} to {} account character(s).",
            spellId, sourceGuid, synced);
}

std::unordered_map<uint32, std::vector<CharacterInfo>> LoadAllCharactersByAccount()
{
    std::unordered_map<uint32, std::vector<CharacterInfo>> charactersByAccount;

    QueryResult result = CharacterDatabase.Query(
        "SELECT guid, account, race, class FROM characters WHERE account <> 0");

    if (!result)
        return charactersByAccount;

    do
    {
        Field* fields = result->Fetch();
        CharacterInfo character{ fields[0].Get<uint32>(), fields[1].Get<uint32>(), fields[2].Get<uint8>(), fields[3].Get<uint8>() };
        charactersByAccount[character.AccountId].push_back(character);
    } while (result->NextRow());

    return charactersByAccount;
}

void BackfillAllProfessionSkills(std::unordered_map<uint32, std::vector<CharacterInfo>> const& charactersByAccount)
{
    if (!Config.AccountBoundSyncSkillProgress)
        return;

    QueryResult result = CharacterDatabase.Query(
        "SELECT c.account, c.guid, c.race, cs.skill, cs.value, cs.max "
        "FROM character_skills cs "
        "INNER JOIN characters c ON c.guid = cs.guid "
        "WHERE c.account <> 0 AND cs.skill IN ({})",
        BuildManagedSkillSqlList());

    if (!result)
        return;

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    uint64 updates = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32 const accountId = fields[0].Get<uint32>();
        uint8 const sourceRace = fields[2].Get<uint8>();
        uint32 const skillId = fields[3].Get<uint16>();
        uint16 value = ClampSyncedSkillValue(fields[4].Get<uint16>());
        uint16 max = ClampSyncedSkillValue(fields[5].Get<uint16>());

        if (!IsManagedProfessionSkill(skillId) || !value || !max)
            continue;

        if (max < value)
            max = value;

        auto accountItr = charactersByAccount.find(accountId);
        if (accountItr == charactersByAccount.end())
            continue;

        for (CharacterInfo const& target : accountItr->second)
        {
            if (!CanShareBetweenRaces(sourceRace, target.Race))
                continue;

            if (!GetSkillRaceClassInfo(skillId, target.Race, target.Class))
                continue;

            InsertOrUpdateSkillForCharacter(trans, target.Guid, skillId, value, max);

            if (Config.AccountBoundSyncProfessionRanks)
                if (uint32 rankSpell = GetBestRankSpellForSkill(skillId, max))
                    if (!Config.SpellBlockList.contains(rankSpell) && IsSpellAllowedForTargetCharacter(rankSpell, target))
                        InsertSpellForCharacter(trans, target.Guid, rankSpell);

            ++updates;
        }
    } while (result->NextRow());

    CommitIfNeeded(trans);
    LOG_INFO("module.noprofessionlimit", "NoProfessionLimit: startup skill backfill queued {} account profession row(s).", updates);
}

void BackfillAllProfessionSpells(std::unordered_map<uint32, std::vector<CharacterInfo>> const& charactersByAccount)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT DISTINCT c.account, c.race, cs.spell "
        "FROM character_spell cs "
        "INNER JOIN characters c ON c.guid = cs.guid "
        "WHERE c.account <> 0");

    if (!result)
        return;

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    uint64 inserted = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32 const accountId = fields[0].Get<uint32>();
        uint8 const sourceRace = fields[1].Get<uint8>();
        uint32 const spellId = fields[2].Get<uint32>();

        if (!IsAccountBoundProfessionSpell(spellId))
            continue;

        auto accountItr = charactersByAccount.find(accountId);
        if (accountItr == charactersByAccount.end())
            continue;

        for (CharacterInfo const& target : accountItr->second)
        {
            if (!CanShareBetweenRaces(sourceRace, target.Race) || !IsSpellAllowedForTargetCharacter(spellId, target))
                continue;

            InsertSpellForCharacter(trans, target.Guid, spellId);
            ++inserted;
        }
    } while (result->NextRow());

    CommitIfNeeded(trans);
    LOG_INFO("module.noprofessionlimit", "NoProfessionLimit: startup spell backfill queued {} account profession spell row(s).", inserted);
}

void BackfillAccountBoundProfessions()
{
    if (!Config.Enabled || !Config.AccountBoundEnabled || !Config.AccountBoundStartupBackfill)
        return;

    std::unordered_map<uint32, std::vector<CharacterInfo>> charactersByAccount = LoadAllCharactersByAccount();
    if (charactersByAccount.empty())
        return;

    BackfillAllProfessionSkills(charactersByAccount);
    BackfillAllProfessionSpells(charactersByAccount);
}

void LoadModuleConfig()
{
    Config.Enabled = sConfigMgr->GetOption<bool>("NoProfessionLimit.Enable", true);
    Config.MaxPrimaryProfessions = sConfigMgr->GetOption<uint32>("NoProfessionLimit.MaxPrimaryProfessions", MaxSupportedPrimaryProfessions);
    Config.CountOnlyAllowedPrimaryProfessions = sConfigMgr->GetOption<bool>("NoProfessionLimit.Professions.CountOnlyAllowed", true);
    Config.EnforceAllowedPrimaryProfessions = sConfigMgr->GetOption<bool>("NoProfessionLimit.Professions.EnforceAllowedList", false);
    Config.MaxSyncedSkillValue = uint16(std::clamp<uint32>(sConfigMgr->GetOption<uint32>("NoProfessionLimit.Professions.MaxSkillValue", DefaultMaxProfessionSkill), 1, DefaultMaxProfessionSkill));

    Config.AllowedPrimarySkills = ParsePrimaryProfessionList(
        sConfigMgr->GetOption<std::string>("NoProfessionLimit.Professions.AllowList", "all"), true);

    for (uint32 blockedSkill : ParsePrimaryProfessionList(
        sConfigMgr->GetOption<std::string>("NoProfessionLimit.Professions.BlockList", ""), false))
        Config.AllowedPrimarySkills.erase(blockedSkill);

    Config.AccountBoundEnabled = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.Enable", false);
    Config.AccountBoundSameFactionOnly = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.SameFactionOnly", true);
    Config.AccountBoundStartupBackfill = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.StartupBackfill", false);
    Config.AccountBoundSyncOnLogin = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.SyncOnLogin", true);
    Config.AccountBoundSyncOnLearn = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.SyncOnLearn", true);
    Config.AccountBoundSyncOnlineOnReload = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.SyncOnlineOnReload", true);
    Config.AccountBoundIncludeSecondaryProfessions = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.IncludeSecondaryProfessions", false);
    Config.AccountBoundSyncSkillProgress = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.SyncSkillProgress", true);
    Config.AccountBoundSyncProfessionRanks = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.SyncProfessionRanks", true);
    Config.AccountBoundSyncRecipes = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.SyncRecipes", true);
    Config.AccountBoundSyncSkillGrantedSpells = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.SyncSkillGrantedSpells", true);
    Config.AccountBoundRequireRecipeSkill = sConfigMgr->GetOption<bool>("NoProfessionLimit.AccountBound.RequireRecipeSkill", true);
    Config.SpellAllowList = ParseSpellList(sConfigMgr->GetOption<std::string>("NoProfessionLimit.AccountBound.SpellAllowList", ""));
    Config.SpellBlockList = ParseSpellList(sConfigMgr->GetOption<std::string>("NoProfessionLimit.AccountBound.SpellBlockList", ""));
}
}

class NoProfessionLimitWorldScript : public WorldScript
{
public:
    NoProfessionLimitWorldScript() : WorldScript("NoProfessionLimitWorldScript", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP
    }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        LoadModuleConfig();
        ApplyWorldProfessionLimit();

        if (reload)
        {
            sWorldSessionMgr->DoForAllOnlinePlayers([](Player* player)
            {
                EnforceAllowedProfessions(player);
                NormalizeFreeProfessionPoints(player);

                if (Config.AccountBoundSyncOnlineOnReload)
                    ApplyAccountSyncToPlayer(player);
            });
        }

        LOG_INFO("module.noprofessionlimit",
            "NoProfessionLimit: {}. MaxPrimary={}, AllowedPrimary={}, AccountBound={}, SameFactionOnly={}, Recipes={}.",
            Config.Enabled ? "enabled" : "disabled",
            Config.Enabled ? GetConfiguredMaxPrimaryProfessions() : 0,
            Config.AllowedPrimarySkills.size(),
            Config.Enabled && Config.AccountBoundEnabled ? "on" : "off",
            Config.AccountBoundSameFactionOnly ? "on" : "off",
            Config.AccountBoundSyncRecipes ? "on" : "off");
    }

    void OnStartup() override
    {
        ApplyWorldProfessionLimit();
        BackfillAccountBoundProfessions();
    }
};

class NoProfessionLimitPlayerScript : public PlayerScript
{
public:
    NoProfessionLimitPlayerScript() : PlayerScript("NoProfessionLimitPlayerScript", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LEARN_SPELL,
        PLAYERHOOK_ON_SET_SKILL
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        EnforceAllowedProfessions(player);
        ApplyAccountSyncToPlayer(player);
        NormalizeFreeProfessionPoints(player);
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        if (!player)
            return;

        if (Config.EnforceAllowedPrimaryProfessions)
            if (uint32 skillId = GetProfessionSkillFromRankSpell(spellId, false))
                if (IsSupportedPrimarySkill(skillId) && !IsAllowedPrimarySkill(skillId))
                    player->removeSpell(spellId, SPEC_MASK_ALL, false);

        SyncSpellToAccount(player, spellId);
        NormalizeFreeProfessionPoints(player);
    }

    void OnPlayerSetSkill(Player* player, uint32 skillId, uint32 /*value*/, uint32 /*max*/, uint32 /*step*/, uint32 /*newValue*/) override
    {
        if (!player)
            return;

        if (!IsPrimaryProfessionSkill(skillId) && !IsSupportedSecondarySkill(skillId))
            return;

        if (Config.EnforceAllowedPrimaryProfessions && IsSupportedPrimarySkill(skillId) && !IsAllowedPrimarySkill(skillId))
        {
            player->SetSkill(skillId, 0, 0, 0);
            return;
        }

        SyncSkillToAccount(player, skillId);
        NormalizeFreeProfessionPoints(player);
    }
};

void AddNoProfessionLimitScripts()
{
    new NoProfessionLimitWorldScript();
    new NoProfessionLimitPlayerScript();
}
