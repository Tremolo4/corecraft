/*
 * Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef MANGOSSERVER_GROUP_H
#define MANGOSSERVER_GROUP_H

#include "BattleGround.h"
#include "Common.h"
#include "DBCEnums.h"
#include "GroupRefManager.h"
#include "GroupReference.h"
#include "LootMgr.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include <map>
#include <unordered_map>
#include <vector>

class BattleGround;
class DungeonPersistentState;
class Field;
class Map;
class Unit;
class WorldSession;

#define MAX_GROUP_SIZE 5
#define MAX_RAID_SIZE 40
#define MAX_RAID_SUBGROUPS (MAX_RAID_SIZE / MAX_GROUP_SIZE)
#define TARGET_ICON_COUNT 8

enum GroupMemberOnlineStatus
{
    MEMBER_STATUS_OFFLINE = 0x0000,
    MEMBER_STATUS_ONLINE = 0x0001,  // Lua_UnitIsConnected
    MEMBER_STATUS_PVP = 0x0002,     // Lua_UnitIsPVP
    MEMBER_STATUS_DEAD = 0x0004,    // Lua_UnitIsDead
    MEMBER_STATUS_GHOST = 0x0008,   // Lua_UnitIsGhost
    MEMBER_STATUS_PVP_FFA = 0x0010, // Lua_UnitIsPVPFreeForAll
    MEMBER_STATUS_UNK3 = 0x0020,    // used in calls from
    // Lua_GetPlayerMapPosition/Lua_GetBattlefieldFlagPosition
    MEMBER_STATUS_AFK = 0x0040, // Lua_UnitIsAFK
    MEMBER_STATUS_DND = 0x0080, // Lua_UnitIsDND
};

enum GroupType
{
    GROUPTYPE_NORMAL = 0,
    GROUPTYPE_RAID = 1
};

enum GroupUpdateFlags
{
    GROUP_UPDATE_FLAG_NONE = 0x00000000,       // nothing
    GROUP_UPDATE_FLAG_STATUS = 0x00000001,     // uint16, flags
    GROUP_UPDATE_FLAG_CUR_HP = 0x00000002,     // uint16
    GROUP_UPDATE_FLAG_MAX_HP = 0x00000004,     // uint16
    GROUP_UPDATE_FLAG_POWER_TYPE = 0x00000008, // uint8
    GROUP_UPDATE_FLAG_CUR_POWER = 0x00000010,  // uint16
    GROUP_UPDATE_FLAG_MAX_POWER = 0x00000020,  // uint16
    GROUP_UPDATE_FLAG_LEVEL = 0x00000040,      // uint16
    GROUP_UPDATE_FLAG_ZONE = 0x00000080,       // uint16
    GROUP_UPDATE_FLAG_POSITION = 0x00000100,   // uint16, uint16
    GROUP_UPDATE_FLAG_AURAS =
        0x00000200, // uint64 mask, for each bit set uint16 spellid + uint8 unk
    GROUP_UPDATE_FLAG_PET_GUID = 0x00000400, // uint64 pet guid
    GROUP_UPDATE_FLAG_PET_NAME = 0x00000800, // pet name, NULL terminated string
    GROUP_UPDATE_FLAG_PET_MODEL_ID = 0x00001000,   // uint16, model id
    GROUP_UPDATE_FLAG_PET_CUR_HP = 0x00002000,     // uint16 pet cur health
    GROUP_UPDATE_FLAG_PET_MAX_HP = 0x00004000,     // uint16 pet max health
    GROUP_UPDATE_FLAG_PET_POWER_TYPE = 0x00008000, // uint8 pet power type
    GROUP_UPDATE_FLAG_PET_CUR_POWER = 0x00010000,  // uint16 pet cur power
    GROUP_UPDATE_FLAG_PET_MAX_POWER = 0x00020000,  // uint16 pet max power
    GROUP_UPDATE_FLAG_PET_AURAS = 0x00040000, // uint64 mask, for each bit set
                                              // uint16 spellid + uint8 unk, pet
                                              // auras...
    GROUP_UPDATE_PET = 0x0007FC00,            // all pet flags
    GROUP_UPDATE_FULL = 0x0007FFFF,           // all known flags
};

#define GROUP_UPDATE_FLAGS_COUNT 20
// 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,11,12,13,14,15,16,17,18,19
static const uint8 GroupUpdateLength[GROUP_UPDATE_FLAGS_COUNT] = {
    0, 2, 2, 2, 1, 2, 2, 2, 2, 4, 8, 8, 1, 2, 2, 2, 1, 2, 2, 8};

struct InstanceGroupBind
{
    std::weak_ptr<DungeonPersistentState> state;
    bool perm = false;
};

// Helper to iterate members
class Group;
class GroupIteratorWrapper
{
public:
    friend class _InnerItr;
    class _InnerItr
    {
    public:
        _InnerItr(const _InnerItr& other);
        _InnerItr(GroupReference* ref, const std::vector<ObjectGuid>* spies,
            bool in_world);
        _InnerItr& operator++();
        _InnerItr operator++(int);
        bool operator==(const _InnerItr& rhs) const;
        bool operator!=(const _InnerItr& rhs) const;
        Player* operator*();

    private:
        Player* advance_pointer();

        GroupReference* ref_;
        const std::vector<ObjectGuid>* spies_;
        size_t spies_index_;
        bool in_world_;
    };

    GroupIteratorWrapper(Group* group, bool in_world_only, bool spies)
      : group_(group), in_world_(in_world_only), spies_(spies)
    {
    }
    _InnerItr begin();
    _InnerItr end();

private:
    Group* group_;
    bool in_world_;
    bool spies_;
};

/** request member stats checken **/
/** todo: uninvite people that not accepted invite **/
// NOTE: Unless otherwise noted, all methods are thread UNSAFE
class MANGOS_DLL_SPEC Group
{
public:
    // Iterate group members, use:
    //     for (Player* player : group->members(true))
    //         ...
    // Param in_world_only: If true, player must be in a map currently
    friend class GroupIteratorWrapper;
    GroupIteratorWrapper members(bool in_world_only, bool spies = false)
    {
        return GroupIteratorWrapper(this, in_world_only, spies);
    }

    struct MemberSlot
    {
        ObjectGuid guid;
        std::string name;
        uint8 group;
        bool assistant;
    };
    typedef std::list<MemberSlot> MemberSlotList;
    typedef MemberSlotList::const_iterator member_citerator;

protected:
    typedef MemberSlotList::iterator member_witerator;
    typedef std::set<Player*> InvitesList;
    typedef std::map<uint32 /*mapId*/, InstanceGroupBind> BoundInstancesMap;

public:
    Group();
    ~Group();

    // group manipulation methods
    bool Create(ObjectGuid guid, const char* name);
    bool LoadGroupFromDB(Field* fields);
    bool LoadMemberFromDB(uint32 guidLow, uint8 subgroup, bool assistant);
    bool AddInvite(Player* player);
    uint32 RemoveInvite(Player* player);
    void RemoveAllInvites();
    bool AddLeaderInvite(Player* player);
    bool AddMember(ObjectGuid guid, const char* name);
    uint32 RemoveMember(
        ObjectGuid guid, uint8 method); // method: 0=just remove, 1=kick
    void ChangeLeader(ObjectGuid guid);
    void SetLootMethod(LootMethod method) { m_lootMethod = method; }
    void SetLooterGuid(ObjectGuid guid) { m_looterGuid = guid; }
    void SetLootThreshold(ItemQualities threshold)
    {
        m_lootThreshold = threshold;
    }
    void Disband(bool hideDestroy = false);

    // properties accessories
    uint32 GetId() const { return m_Id; }
    ObjectGuid GetObjectGuid() const
    {
        return ObjectGuid(HIGHGUID_GROUP, GetId());
    }
    bool IsFull() const
    {
        return (m_groupType == GROUPTYPE_NORMAL) ?
                   (m_memberSlots.size() >= MAX_GROUP_SIZE) :
                   (m_memberSlots.size() >= MAX_RAID_SIZE);
    }
    bool isRaidGroup() const { return m_groupType == GROUPTYPE_RAID; }
    bool isBGGroup() const { return m_bgGroup != nullptr; }
    bool IsCreated() const { return GetMembersCount() > 0; }
    ObjectGuid GetLeaderGuid() const { return m_leaderGuid; }
    const char* GetLeaderName() const { return m_leaderName.c_str(); }
    LootMethod GetLootMethod() const { return m_lootMethod; }
    ObjectGuid GetLooterGuid() const { return m_looterGuid; }
    ItemQualities GetLootThreshold() const { return m_lootThreshold; }

    // member manipulation methods
    bool IsMember(ObjectGuid guid) const
    {
        return _getMemberCSlot(guid) != m_memberSlots.end();
    }
    bool IsLeader(ObjectGuid guid) const { return GetLeaderGuid() == guid; }
    ObjectGuid GetMemberGuid(const std::string& name)
    {
        for (member_citerator itr = m_memberSlots.begin();
             itr != m_memberSlots.end(); ++itr)
            if (itr->name == name)
                return itr->guid;

        return ObjectGuid();
    }
    bool IsAssistant(ObjectGuid guid) const
    {
        auto mslot = _getMemberCSlot(guid);
        if (mslot == m_memberSlots.end())
            return false;

        return mslot->assistant;
    }
    Player* GetInvited(ObjectGuid guid) const;
    Player* GetInvited(const std::string& name) const;

    bool HasFreeSlotSubGroup(uint8 subgroup) const
    {
        return (
            m_subGroupsCounts && m_subGroupsCounts[subgroup] < MAX_GROUP_SIZE);
    }

    bool SameSubGroup(Player const* member1, Player const* member2) const;

    MemberSlotList const& GetMemberSlots() const { return m_memberSlots; }
    uint32 GetMembersCount() const { return m_memberSlots.size(); }
    void GetDataForXPAtKill(Unit const* victim, uint32& count,
        uint32& sum_level, Player*& member_with_max_level,
        Player*& not_gray_member_with_max_level, Player* additional = nullptr);
    uint8 GetMemberGroup(ObjectGuid guid) const
    {
        auto mslot = _getMemberCSlot(guid);
        if (mslot == m_memberSlots.end())
            return MAX_RAID_SUBGROUPS + 1;

        return mslot->group;
    }

    // some additional raid methods
    void ConvertToRaid();

    void SetBattlegroundGroup(BattleGround* bg) { m_bgGroup = bg; }

    void ChangeMembersGroup(ObjectGuid guid, uint8 group);
    void ChangeMembersGroup(Player* player, uint8 group);
    void SwapMembers(ObjectGuid one, ObjectGuid two);

    ObjectGuid GetMainTankGuid() const { return m_mainTankGuid; }
    ObjectGuid GetMainAssistantGuid() const { return m_mainAssistantGuid; }

    void SetAssistant(ObjectGuid guid, bool state)
    {
        if (!isRaidGroup())
            return;
        if (_setAssistantFlag(guid, state))
            SendUpdate();
    }
    void SetMainTank(ObjectGuid guid)
    {
        if (!isRaidGroup())
            return;

        if (_setMainTank(guid))
            SendUpdate();
    }
    void SetMainAssistant(ObjectGuid guid)
    {
        if (!isRaidGroup())
            return;

        if (_setMainAssistant(guid))
            SendUpdate();
    }

    // Thread-safe
    void ClearTargetIcon(ObjectGuid target);
    void SetTargetIcon(uint8 id, ObjectGuid targetGuid);
    void SendTargetIconList(WorldSession* session);

    void SetDifficulty(Difficulty difficulty);
    Difficulty GetDifficulty() const { return m_difficulty; }
    uint16 InInstance();
    bool InCombatToInstance(uint32 instanceId);
    void ResetInstances(InstanceResetMethod method, Player* SendMsgTo);

    void SendUpdate();
    void UpdatePlayerOutOfRange(Player* pPlayer);
    // ignore: GUID of player that will be ignored
    void BroadcastPacket(WorldPacket* packet, bool ignorePlayersInBGRaid,
        int group = -1, ObjectGuid ignore = ObjectGuid(),
        WorldObject* source = nullptr, float maximum_dist = -1.0f);
    void BroadcastReadyCheck(WorldPacket* packet);
    void OfflineReadyCheck();

    void RewardGroupAtKill(Unit* pVictim, Player* player_tap);

    void LinkMember(GroupReference* pRef) { m_memberMgr.insertFirst(pRef); }
    void DelinkMember(GroupReference* /*pRef*/) {}

    // Thread-safe
    ObjectGuid GetNextGroupLooter(const std::set<ObjectGuid>& looters);

    // === Instance Bind Methods ===
    // NOTE: A group only has a bind to an instance if that instance has no
    // permanent player binds
    InstanceGroupBind* GetInstanceBind(uint32 mapid, Difficulty difficulty);
    void BindToInstance(
        std::shared_ptr<DungeonPersistentState> state, bool perm);
    void UnbindFromInstance(DungeonPersistentState* state);
    // same as UnbindFromInstance but does not affect the database (i.e.
    // permanent state)
    void ClearInstanceBindOnDestruction(DungeonPersistentState* state);
    BoundInstancesMap& GetInstanceBindsMap(Difficulty difficulty)
    {
        return m_instanceBinds[difficulty];
    }

    // NOT thread-safe
    void PassLeaderOnward();

    void add_spy(Player* player);
    void remove_spy(Player* player);

protected:
    bool _addMember(
        ObjectGuid guid, const char* name, bool isAssistant = false);
    bool _addMember(
        ObjectGuid guid, const char* name, bool isAssistant, uint8 group);
    bool _removeMember(ObjectGuid guid); // returns true if leader has changed
    void _setLeader(ObjectGuid guid);

    bool _setMembersGroup(ObjectGuid guid, uint8 group);
    bool _setAssistantFlag(ObjectGuid guid, const bool& state);
    bool _setMainTank(ObjectGuid guid);
    bool _setMainAssistant(ObjectGuid guid);

    // Must hold group_mutex_ to call
    void _setTargetIcon(uint8 id, ObjectGuid targetGuid);

    void _initRaidSubGroupsCounter()
    {
        // Sub group counters initialization
        if (!m_subGroupsCounts)
            m_subGroupsCounts = new uint8[MAX_RAID_SUBGROUPS];

        memset((void*)m_subGroupsCounts, 0, MAX_RAID_SUBGROUPS * sizeof(uint8));

        for (member_citerator itr = m_memberSlots.begin();
             itr != m_memberSlots.end(); ++itr)
            ++m_subGroupsCounts[itr->group];
    }

    member_citerator _getMemberCSlot(ObjectGuid guid) const
    {
        for (auto itr = m_memberSlots.begin(); itr != m_memberSlots.end();
             ++itr)
            if (itr->guid == guid)
                return itr;

        return m_memberSlots.end();
    }

    member_witerator _getMemberWSlot(ObjectGuid guid)
    {
        for (auto itr = m_memberSlots.begin(); itr != m_memberSlots.end();
             ++itr)
            if (itr->guid == guid)
                return itr;

        return m_memberSlots.end();
    }

    void SubGroupCounterIncrease(uint8 subgroup)
    {
        if (m_subGroupsCounts)
            ++m_subGroupsCounts[subgroup];
    }

    void SubGroupCounterDecrease(uint8 subgroup)
    {
        if (m_subGroupsCounts)
            --m_subGroupsCounts[subgroup];
    }

    std::mutex group_mutex_;

    uint32 m_Id; // 0 for not created or BG groups
    MemberSlotList m_memberSlots;
    GroupRefManager m_memberMgr;
    std::vector<ObjectGuid> m_spies; // GMs that are "spying" on this group
    InvitesList m_invitees;
    ObjectGuid m_leaderGuid;
    std::string m_leaderName;
    ObjectGuid m_mainTankGuid;
    ObjectGuid m_mainAssistantGuid;
    GroupType m_groupType;
    Difficulty m_difficulty;
    BattleGround* m_bgGroup;
    ObjectGuid m_targetIcons[TARGET_ICON_COUNT];
    LootMethod m_lootMethod;
    ItemQualities m_lootThreshold;
    ObjectGuid m_looterGuid;
    BoundInstancesMap m_instanceBinds[MAX_DIFFICULTY];
    uint8* m_subGroupsCounts;

    // Loot order related
    std::unordered_map<ObjectGuid, uint32> m_lootIndexes;
    uint32 m_currentLootIndex;
};
#endif
