#pragma once
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <set>

namespace fb
{
    class ServerPlayer;
    class PVZCharacterServerPlayerExtent;
}

enum class ValidationFlag : uint32_t {
    None = 0,
    InvalidTeam = 1 << 0,
    InvalidPrimary = 1 << 1,
    InvalidAbility1 = 1 << 2,
    InvalidAbility2 = 1 << 3,
    InvalidAbility3 = 1 << 4,
    InvalidAlternate = 1 << 5,
    InvalidSoldier = 1 << 6,
    InvalidUpgrade = 1 << 7,
    NullPointer = 1 << 8
};

inline ValidationFlag operator|(ValidationFlag a, ValidationFlag b) {
    return static_cast<ValidationFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ValidationFlag& operator|=(ValidationFlag& a, ValidationFlag b) {
    a = a | b;
    return a;
}

inline ValidationFlag operator&(ValidationFlag a, ValidationFlag b) {
    return static_cast<ValidationFlag>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasFlag(ValidationFlag flags, ValidationFlag flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

struct WeaponSet
{
    std::unordered_set<uint32_t> primary; //WeaponSlot_0
    std::vector<std::string> primaryNames;
    std::unordered_set<uint32_t> ability1; //WeaponSlot_1
    std::unordered_set<uint32_t> ability2; //WeaponSlot_2
    std::unordered_set<uint32_t> ability3; //WeaponSlot_3
    std::unordered_set<uint32_t> alternate; //WeaponSlot_4
    std::unordered_set<uint32_t> weaponSlot5; //WeaponSlot_5
    std::unordered_set<uint32_t> weaponSlot6; //WeaponSlot_6
    std::unordered_set<uint32_t> weaponSlot7; //WeaponSlot_7
    std::unordered_set<uint32_t> weaponSlot8; //WeaponSlot_8
    std::unordered_set<uint32_t> weaponSlot9; //WeaponSlot_9
    bool allowAlternate = false;

    std::vector<std::string> allowedPrimaryPrefixes; // full asset path prefixes
    std::vector<std::string> allowedSoldierSubstrings; // blueprint name must contain one of these
    std::string requiredMode; // if non-empty, kit only valid in this game mode
    bool skipAbilityValidation = false;
    bool skipUpgradeValidation = false;
};

struct UpgradeSet
{
    std::unordered_set<uint32_t> selectable;
    std::unordered_set<uint32_t> nonSelectable;
    bool hasNonSelectable = false;
};

struct ValidationResult {
    bool isValid = true;
    ValidationFlag flags = ValidationFlag::None;

    std::string invalidPrimary;
    std::string invalidAbility1;
    std::string invalidAbility2;
    std::string invalidAbility3;
    std::string invalidAlternate;
    std::string invalidSoldier;
    std::vector<std::string> invalidUpgrades;

    std::string characterName;
    std::string teamName;
    std::string weaponName;
    std::string playerName;
    uint64_t personaId = 0;
    int teamId = -1;
    bool invalidTeam = false;

    void addFlag(ValidationFlag flag) {
        isValid = false;
        flags |= flag;
    }
};

class LoadoutValidator {
public:
    static LoadoutValidator& getInstance()
    {
        static LoadoutValidator instance;
        return instance;
    }
    void init(); //build weapon and upgrade sets
    void invalidate() { m_initialized = false; } // call on level change to force reinit on next player join
    bool needsInit() const { return !m_initialized; }

    static ValidationResult validatePlayer(fb::ServerPlayer* player);
    static ValidationResult validateWeapons(fb::PVZCharacterServerPlayerExtent* extent, const WeaponSet& allowedWeapons, const char* playerName);
    static void validateUpgrades(fb::PVZCharacterServerPlayerExtent* extent, ValidationResult& result, const UpgradeSet& allowedUpgrades, const char* playerName);
    static bool isValidTeamForFaction(int teamId, bool isPlant, bool isZombie);
     
    inline static std::unordered_map<uint32_t, WeaponSet> weaponSets;
    inline static std::unordered_map<uint32_t, UpgradeSet> upgradeSets;
    inline static std::vector<uint32_t> upgradableWeaponIds;

    bool m_initialized = false;

    std::set<std::string> kitBlacklist
    {
        "Gameplay/Kits/Plant_LaserGoat_Green",
        "Gameplay/Kits/Zombie_LaserGoat_Purple",
        "Gameplay/Kits/Zombie_ZombossCat",
        "Gameplay/Kits/Zombie_ZombossCat_CvD",
        "Gameplay/Kits/Plant_Junkasaurus",
        "Gameplay/Kits/Plant_Junkasaurus_CvD",
        "Gameplay/Kits/Plant_ChomperDelivery"
    };
};
