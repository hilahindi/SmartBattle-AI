#pragma once
#include "NPC.h"
#include <vector>
#include <utility>
#include <unordered_map>

// ---------------------------------------------------------
// TeamState - defines global strategy for a commander
// ---------------------------------------------------------
enum class TeamState { ATTACK, DEFEND, RETREAT };
enum class ReportType { LOW_AMMO, INJURED, ENEMY_SPOTTED };

// ---------------------------------------------------------
// Commander class - gives high-level orders to its team
// ---------------------------------------------------------
class Commander {
private:
    enum class OrderType { None, DeliverAmmo };

    NPC* commander;                   // pointer to the commander NPC itself
    std::vector<NPC*> team;           // all NPCs in this commander's team
    TeamState teamState;              // current strategic mode
    double lastRepositionCheck;       // timer for commander repositioning
    double battleStartTime;           // first time we evaluated strategy
    double lastAttackIssuedTime;      // cooldown between attack orders
    double lastStateChangeTime;       // prevent rapid oscillations
    OrderType lastOrderType;
    NPC* lastOrderTarget;
    double lastOrderIssuedTime;

    std::pair<int, int> ComputeEnemyFocus() const;
    std::pair<int, int> FindSafePositionForWarrior(NPC* warrior, const std::vector<std::pair<int, int>>& reserved) const;
    NPC* FindMostCriticalInjuredAlly() const;
    NPC* FindMostAmmoStarvedWarrior() const;
    bool IsReserved(const std::vector<std::pair<int, int>>& reserved, int x, int y) const;
    std::pair<int, int> AcquireWarriorOffset(NPC* warrior);

public:
    Commander(NPC* cmd, const std::vector<NPC*>& members)
        : commander(cmd),
        team(members),
        teamState(TeamState::DEFEND),
        lastRepositionCheck(0.0),
        battleStartTime(0.0),
        lastAttackIssuedTime(-100.0),
        lastStateChangeTime(0.0),
        lastOrderType(OrderType::None),
        lastOrderTarget(nullptr),
        lastOrderIssuedTime(-100.0),
        warriorOffsetCursor(0) {
    }

    // Evaluate current team condition (health, alive units, etc.)
    void EvaluateTeamStatus();

    // Assign proper FSM states (GoToCombat, GoToHeal, GoDeliverAmmo, etc.)
    void PlanAndAssignOrders();

    void ReceiveReport(NPC* sender, ReportType type);
    
    // Commander-specific: move to safe position using visibility map
    void MoveToSafePosition();
    
    // Use visibility map to plan attack route for warriors
    void PlanAttackRoute();

private:
    std::unordered_map<NPC*, std::pair<int, int>> warriorOffsets;
    size_t warriorOffsetCursor;
    bool HasActiveSupplyFor(NPC* soldier) const;
    bool IsOrderDebounced(OrderType type, NPC* target, double now) const;
    NPC* FindAvailablePorter() const;
    void AssignDeliverAmmo(NPC* porter, NPC* soldier, double now);
};
