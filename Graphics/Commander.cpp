#include "Commander.h"
#include "GoToCombat.h"
#include "GoToCover.h"
#include "GoToHeal.h"
#include "GoDeliverAmmo.h"
#include "GoToSupply.h"
#include "GoToMedSupply.h"
#include "Map.h"
#include "Pathfinding.h"
#include "ReturnToWarehouse.h"
#include <stdio.h>
#include "NPC.h"
#include <algorithm>
#include <numeric>
#include <vector>
#include <typeinfo> // required for typeid
#include <limits>
#include <cmath>
#include "Definitions.h"
#include <time.h>
#include <array>
#include <cstdlib>

extern std::vector<NPC*> teamOrange;
extern std::vector<NPC*> teamBlue;

namespace {
    enum class CoverBand : int { Retreat = 0, Defend = 1, Attack = 2 };

    std::array<std::vector<std::pair<int, int>>, 3> coverSlotsOrange;
    std::array<std::vector<std::pair<int, int>>, 3> coverSlotsBlue;
    bool coverCatalogBuilt = false;

    inline int ClampInt(int value, int minValue, int maxValue)
    {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    inline int StateToIndex(TeamState state)
    {
        switch (state) {
        case TeamState::RETREAT: return 0;
        case TeamState::DEFEND:  return 1;
        case TeamState::ATTACK:  return 2;
        default: return 1;
        }
    }

    CoverBand ClassifyBandForTeam(TeamId team, int x)
    {
        if (team == TeamId::Orange) {
            if (x < 45)  return CoverBand::Retreat;
            if (x < 110) return CoverBand::Defend;
            return CoverBand::Attack;
        }
        else {
            if (x > 155) return CoverBand::Retreat;
            if (x > 90)  return CoverBand::Defend;
            return CoverBand::Attack;
        }
    }

    bool HasNearbyCover(int x, int y)
    {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx;
                int ny = y + dy;
                if (!Map::InBounds(nx, ny)) continue;
                Map::Cell c = Map::Get(nx, ny);
                if (c == Map::TREE || c == Map::ROCK || c == Map::WAREHOUSE) {
                    return true;
                }
            }
        }
        return false;
    }

    void BuildCoverCatalog()
    {
        if (coverCatalogBuilt) return;

        for (auto& vec : coverSlotsOrange) vec.clear();
        for (auto& vec : coverSlotsBlue) vec.clear();

        for (int y = 1; y < Map::H - 1; ++y) {
            for (int x = 1; x < Map::W - 1; ++x) {
                if (!Map::IsWalkable(x, y)) continue;
                if (!HasNearbyCover(x, y)) continue;
                if (((x + y) & 1) != 0) continue; // thin out dense clusters

                CoverBand orangeBand = ClassifyBandForTeam(TeamId::Orange, x);
                CoverBand blueBand = ClassifyBandForTeam(TeamId::Blue, x);

                coverSlotsOrange[static_cast<int>(orangeBand)].push_back({ x, y });
                coverSlotsBlue[static_cast<int>(blueBand)].push_back({ x, y });
            }
        }

        coverCatalogBuilt = true;
    }

    const std::vector<std::pair<int, int>>& GetCoverSlots(TeamId team, TeamState state)
    {
        BuildCoverCatalog();
        int idx = StateToIndex(state);
        return (team == TeamId::Orange) ? coverSlotsOrange[idx] : coverSlotsBlue[idx];
    }
}

static TeamState lastPlannedTeamState = TeamState::DEFEND;

std::pair<int, int> Commander::ComputeEnemyFocus() const
{
    const std::vector<NPC*>& enemies =
        (commander && commander->getTeam() == TeamId::Orange) ? teamBlue : teamOrange;

    double sumX = 0.0;
    double sumY = 0.0;
    int count = 0;
    for (NPC* e : enemies) {
        if (!e || !e->IsAlive()) continue;
        sumX += e->getX();
        sumY += e->getY();
        count++;
    }
    if (count == 0) {
        return (commander && commander->getTeam() == TeamId::Orange)
            ? std::make_pair(140, 60)
            : std::make_pair(60, 40);
    }
    return { (int)(sumX / count), (int)(sumY / count) };
}

bool Commander::IsReserved(const std::vector<std::pair<int, int>>& reserved, int x, int y) const
{
    for (const auto& pos : reserved) {
        if (pos.first == x && pos.second == y) return true;
    }
    return false;
}

std::pair<int, int> Commander::FindSafePositionForWarrior(NPC* warrior, const std::vector<std::pair<int, int>>& reserved) const
{
    if (!warrior) return { -1, -1 };

    std::pair<int, int> focus = ComputeEnemyFocus();
    const int searchRadius = 25;
    double bestScore = std::numeric_limits<double>::max();
    std::pair<int, int> best = { -1, -1 };

    TeamId teamId = warrior->getTeam();

    for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
        for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
            int x = focus.first + dx;
            int y = focus.second + dy;
            if (!Map::InBounds(x, y)) continue;
            if (!Map::IsWalkable(x, y)) continue;
            if (IsReserved(reserved, x, y)) continue;
            if (Map::IsOccupied(x, y, warrior->GetId())) continue;

            double security = Map::GetSecurityValue(y, x, teamId);
            if (security > 0.75) continue; // avoid highly dangerous cells
            double distEnemy = std::sqrt((double)(dx * dx + dy * dy));
            double distSelf = std::sqrt((warrior->getX() - x) * (warrior->getX() - x) +
                (warrior->getY() - y) * (warrior->getY() - y));

            double score = security * 100.0 + distEnemy * 0.5 + distSelf * 0.1;
            if (score < bestScore) {
                bestScore = score;
                best = { x, y };
            }
        }
    }

    if (best.first == -1) {
        best = focus;
    }
    return best;
}

std::pair<int, int> Commander::AcquireWarriorOffset(NPC* warrior)
{
    if (!warrior) return { 0,0 };

    auto it = warriorOffsets.find(warrior);
    if (it != warriorOffsets.end()) {
        return it->second;
    }

    static const std::array<std::pair<int, int>, 8> OFFSET_POOL = {
        std::pair<int, int>{-4, 0},
        std::pair<int, int>{0, 0},
        std::pair<int, int>{4, 0},
        std::pair<int, int>{-4, 3},
        std::pair<int, int>{4, 3},
        std::pair<int, int>{-2, -3},
        std::pair<int, int>{2, -3},
        std::pair<int, int>{0, 5}
    };

    std::pair<int, int> chosen = OFFSET_POOL[warriorOffsetCursor % OFFSET_POOL.size()];
    ++warriorOffsetCursor;
    warriorOffsets[warrior] = chosen;
    return chosen;
}

NPC* Commander::FindMostCriticalInjuredAlly() const
{
    NPC* best = nullptr;
    int bestHP = 101;
    for (NPC* ally : team) {
        if (!ally || !ally->IsAlive()) continue;
        if (ally->getHP() >= 100) continue;
        int hp = ally->getHP();
        if (ally->getRole() == Role::Commander) {
            hp -= 20; 
        }
        if (!best || hp < bestHP) {
            best = ally;
            bestHP = hp;
        }
    }
    return best;
}

NPC* Commander::FindMostAmmoStarvedWarrior() const
{
    NPC* best = nullptr;
    int lowestScore = std::numeric_limits<int>::max();
    for (NPC* ally : team) {
        if (!ally || !ally->IsAlive()) continue;
        if (ally->getRole() != Role::Warrior) continue;

        int ammo = ally->getAmmo();
        int grenades = ally->getGrenades();
        bool needsAmmo = ammo <= LOW_AMMO_THRESHOLD;
        bool needsGrenades = grenades <= 1;
        if (!needsAmmo && !needsGrenades) continue;

        int score = ammo * 10 + grenades * 3;
        if (needsGrenades) score -= 20;
        if (ally->getSupply() <= 1) score -= 10;

        if (score < lowestScore) {
            lowestScore = score;
            best = ally;
        }
    }
    return best;
}

bool Commander::HasActiveSupplyFor(NPC* soldier) const
{
    if (!soldier) return false;

    for (NPC* npc : team) {
        if (!npc || npc == soldier) continue;
        if (!npc->IsAlive()) continue;
        if (npc->getRole() != Role::Porter) continue;
        State* state = npc->getCurrentState();
        if (!state) continue;
        if (typeid(*state) != typeid(GoDeliverAmmo)) continue;
        if (!npc->isBusy()) continue;
        if (npc->getTargetNPC() == soldier) {
            return true;
        }
    }
    return false;
}

bool Commander::IsOrderDebounced(OrderType type, NPC* target, double now) const
{
    if (type != lastOrderType) return false;
    if (target != lastOrderTarget) return false;
    return (now - lastOrderIssuedTime) < ORDER_DEBOUNCE_SECONDS;
}

NPC* Commander::FindAvailablePorter() const
{
    NPC* best = nullptr;
    int highestSupply = -1;
    for (NPC* npc : team) {
        if (!npc || !npc->IsAlive()) continue;
        if (npc->getRole() != Role::Porter) continue;
        if (npc->getSupply() <= 0) continue;
        if (npc->isBusy()) continue;
        if (npc->getTargetNPC()) continue;
        if (!npc->CanTakeAssist()) continue;

        if (npc->getSupply() > highestSupply) {
            highestSupply = npc->getSupply();
            best = npc;
        }
    }
    return best;
}

void Commander::AssignDeliverAmmo(NPC* porter, NPC* soldier, double now)
{
    if (!porter || !soldier) return;
    if (!porter->CanTakeAssist()) {
        printf("   [WARN] Porter %c assist limit reached (%d/%d). Assignment aborted.\n",
            porter->getSymbol(), porter->GetAssistsDone(), NPC::ASSIST_LIMIT);
        return;
    }
    if (IsOrderDebounced(OrderType::DeliverAmmo, soldier, now)) {
        return;
    }
    if (HasActiveSupplyFor(soldier) && porter->getTargetNPC() != soldier) {
        return;
    }

    State* current = porter->getCurrentState();
    if (current) {
        current->OnExit(porter);
        delete current;
    }

    porter->setCurrentState(new GoDeliverAmmo(soldier));
    porter->getCurrentState()->OnEnter(porter);

    lastOrderType = OrderType::DeliverAmmo;
    lastOrderTarget = soldier;
    lastOrderIssuedTime = now;
}

// Evaluate team condition: decide if attack / defend / retreat
void Commander::EvaluateTeamStatus() {
    double now = clock() / (double)CLOCKS_PER_SEC;
    if (battleStartTime <= 0.0) {
        battleStartTime = now;
    }

    int alive = 0;
    int lowHealth = 0;
    double hpSum = 0.0;
    double dangerSum = 0.0;

    for (NPC* npc : team) {
        if (npc->IsAlive()) {
            alive++;
            hpSum += npc->getHP();
            dangerSum += Map::GetSecurityValue((int)npc->getY(), (int)npc->getX(), npc->getTeam());
            if (npc->getHP() < 45) lowHealth++;
        }
    }

    double avgHP = (alive > 0) ? hpSum / alive : 0.0;
    double avgDanger = (alive > 0) ? dangerSum / alive : 0.0;
    double commanderDanger = 0.0;
    int commanderHP = 0;

    if (commander && commander->IsAlive()) {
        commanderDanger = Map::GetSecurityValue((int)commander->getY(), (int)commander->getX(), commander->getTeam());
        commanderHP = commander->getHP();
    }

    const std::vector<NPC*>& enemies =
        (commander && commander->getTeam() == TeamId::Orange) ? teamBlue : teamOrange;

    int enemiesAlive = 0;
    double enemyHpSum = 0.0;
    for (NPC* enemy : enemies) {
        if (!enemy || !enemy->IsAlive()) continue;
        enemiesAlive++;
        enemyHpSum += enemy->getHP();
    }
    double avgEnemyHP = (enemiesAlive > 0) ? enemyHpSum / enemiesAlive : 0.0;

    bool openingPhase = (now - battleStartTime) < 10.0;
    TeamState previousState = teamState;

    bool forceRetreat = (commanderDanger > 0.6) ||
        (commanderHP > 0 && commanderHP < 35) ||
        (lowHealth >= std::max(1, alive / 2));

    TeamState desiredState = teamState;

    bool cooldownReady = (lastAttackIssuedTime < 0.0) || ((now - lastAttackIssuedTime) > 8.0);
    bool timeForProbe = (!openingPhase) && cooldownReady && ((now - lastAttackIssuedTime) > 10.0);

    if (forceRetreat) {
        desiredState = TeamState::RETREAT;
    }
    else {
        bool advantage =
            (alive >= enemiesAlive) &&
            (avgHP > avgEnemyHP + 4.0) &&
            (avgDanger < 0.38) &&
            (commanderDanger < 0.35) &&
            (avgHP > 55.0);

        if (advantage && cooldownReady) {
            desiredState = TeamState::ATTACK;
        }
        else if (timeForProbe) {
            desiredState = TeamState::ATTACK;
        }
        else {
            desiredState = TeamState::DEFEND;
        }
    }

    if (desiredState == TeamState::ATTACK && openingPhase) {
        desiredState = TeamState::DEFEND;
    }

    bool canChangeState = (now - lastStateChangeTime) > 4.0 || forceRetreat;
    if (desiredState != teamState && canChangeState) {
        teamState = desiredState;
        lastStateChangeTime = now;
        if (teamState == TeamState::ATTACK) {
            lastAttackIssuedTime = now;
        }
        else if (teamState == TeamState::RETREAT) {
            lastAttackIssuedTime = -50.0;
        }
    }

    if (teamState != previousState) {
        switch (teamState) {
        case TeamState::ATTACK:
            printf("[INFO] Commander orders ATTACK.\n");
            break;
        case TeamState::DEFEND:
            printf("[INFO] Commander switches to DEFEND mode.\n");
            break;
        case TeamState::RETREAT:
            printf("[WARN] Commander orders RETREAT.\n");
            break;
        }
    }
}

// Assign initial or updated orders to all teammates
void Commander::PlanAndAssignOrders()
{
    // If commander is dead, warriors continue on their own (without combined visibility map)
    if (!commander || !commander->IsAlive()) {
        printf("[WARN] Commander is dead. Warriors continue independently.\n");
        
        // Warriors can continue fighting but without commander's strategic planning
        for (NPC* npc : team) {
            if (!npc || !npc->IsAlive()) continue;
            
            // Warriors continue their current state (combat/defense)
            if (npc->getRole() == Role::Warrior) {
                // Warriors can continue fighting independently
                if (!npc->getCurrentState()) {
                    npc->setCurrentState(new GoToCombat());
                    npc->getCurrentState()->OnEnter(npc);
                }
            }
        }
        return;
    }
    
    // Commander moves to safe position if needed
    MoveToSafePosition();
    
    // Use visibility map to plan attack routes (only if commander is alive)
    PlanAttackRoute();

    double now = clock() / (double)CLOCKS_PER_SEC;

    EvaluateTeamStatus();
    printf("[INFO] Commander %c assigning updated orders based on security map.\n", commander->getSymbol());

    std::vector<std::pair<int, int>> reservedPositions;

    std::vector<NPC*> lowAmmoWarriors;
    std::vector<NPC*> criticalInjured;

    for (auto it = warriorOffsets.begin(); it != warriorOffsets.end(); ) {
        if (!it->first || !it->first->IsAlive()) {
            it = warriorOffsets.erase(it);
        }
        else {
            ++it;
        }
    }

    for (NPC* npc : team) {
        if (!npc || !npc->IsAlive()) continue;
        if (npc->getRole() == Role::Warrior) {
            if (npc->getAmmo() <= LOW_AMMO_THRESHOLD || npc->getGrenades() == 0) {
                lowAmmoWarriors.push_back(npc);
            }
        }
        if (npc->getHP() < INJURY_THRESHOLD) {
            criticalInjured.push_back(npc);
        }
    }

    for (NPC* npc : team)
    {
        if (!npc || !npc->IsAlive()) continue;

        Role r = npc->getRole();
        char symbol = npc->getSymbol();
        int npcX = (int)npc->getX();
        int npcY = (int)npc->getY();
        
        // Check security map for this NPC's position
        double security = Map::GetSecurityValue(npcY, npcX, npc->getTeam());
        bool forcedRetreat = false;
        if (r != Role::Warrior && security > 0.55) {
            forcedRetreat = true;
        }
        
        State* currentState = npc->getCurrentState();
        
        bool shouldReassign = false;
        

        if (currentState == nullptr) {
            shouldReassign = true;  // No state - definitely need to assign
        } else {
            // NPC has a state - check if it's stuck or urgent situation
            bool isActive = npc->getIsMoving() || npc->getIsEngaging();
            bool isUrgent = false;
            
            // Check for urgent situations that require immediate reassignment
            if (r == Role::Warrior) {
                if (security > 0.7 || npc->getHP() < 35) {
                    isUrgent = true;  // Very high danger or very low HP
                }
            } else if (r == Role::Medic) {
                // Urgent if there's a critically injured ally (HP < 30)
                for (NPC* ally : team) {
                    if (ally && ally != npc && ally->IsAlive() && ally->getHP() < 30) {
                        isUrgent = true;
                        break;
                    }
                }
            } else if (r == Role::Porter) {
                // Urgent if warrior has no ammo at all
                for (NPC* ally : team) {
                    if (ally && ally->getRole() == Role::Warrior && ally->IsAlive() && ally->getAmmo() <= LOW_AMMO_THRESHOLD) {
                        isUrgent = true;
                        break;
                    }
                }
            }
            
            // Only reassign if urgent OR if truly idle (not active)
            if (isUrgent || !isActive) {
                shouldReassign = true;
            } else {
                // NPC is actively working - don't interrupt
                shouldReassign = false;
            }

            if (r == Role::Medic && !criticalInjured.empty()) {
                isUrgent = true;
            }

            if (r == Role::Porter && !lowAmmoWarriors.empty()) {
                isUrgent = true;
            }
        }

        if (r == Role::Medic && currentState) {
            if (criticalInjured.empty() && typeid(*currentState) == typeid(GoToHeal)) {
                shouldReassign = true;
            }
        }

        if (r == Role::Porter && currentState) {
            bool deliveringAmmo = (typeid(*currentState) == typeid(GoDeliverAmmo));
            if (deliveringAmmo && npc->isBusy() && npc->getTargetNPC() != nullptr && !forcedRetreat) {
                shouldReassign = false;
            }
            else if (deliveringAmmo && lowAmmoWarriors.empty()) {
                shouldReassign = true;
            }
        }

        if (teamState != lastPlannedTeamState) {
            shouldReassign = true;
        }
        
        // Only reassign if needed
        if (!shouldReassign && !forcedRetreat) {
            continue;  // Skip reassignment, let current state continue
        }
        
        // Clean up old state
        if (currentState) {
            currentState->OnExit(npc);
            delete currentState;
            npc->setCurrentState(nullptr);
        }

        // Reset movement flags
        npc->setIsMoving(false);

        // Decision based on role AND security map
        switch (r)
        {
        case Role::Warrior:
            {
                if (forcedRetreat) {
                    printf("   [INFO] Forcing %c to retreat from danger (security=%.2f)\n", symbol, security);
                    npc->setCurrentState(new GoToCover());
                    npc->getCurrentState()->OnEnter(npc);
                    break;
                }
                auto pickSlot = [&](TeamState desiredState) -> std::pair<int, int>
                {
                    const auto& slots = GetCoverSlots(npc->getTeam(), desiredState);
                    double bestScore = 2.0;
                    std::pair<int, int> best{ -1, -1 };
                    if (slots.empty()) return best;

                    std::vector<size_t> order(slots.size());
                    std::iota(order.begin(), order.end(), 0);
                    for (size_t i = 0; i < order.size(); ++i) {
                        size_t j = i + (rand() % (order.size() - i));
                        std::swap(order[i], order[j]);
                    }

                    int evaluated = 0;
                    const int MAX_EVAL = 20;
                    for (size_t idx : order) {
                        if (evaluated++ >= MAX_EVAL) break;
                        const auto& pos = slots[idx];
                        if (!Map::InBounds(pos.first, pos.second)) continue;
                        if (!Map::IsWalkable(pos.first, pos.second)) continue;
                        if (IsReserved(reservedPositions, pos.first, pos.second)) continue;
                        if (Map::IsOccupied(pos.first, pos.second, npc->GetId())) continue;
                        double slotSecurity = Map::GetSecurityValue(pos.second, pos.first, npc->getTeam());
                        if (slotSecurity < bestScore) {
                            bestScore = slotSecurity;
                            best = pos;
                        }
                    }
                    return best;
                };

                std::pair<int, int> desired = { -1, -1 };
                if (teamState == TeamState::ATTACK) {
                    desired = pickSlot(TeamState::ATTACK);
                }
                else if (teamState == TeamState::RETREAT) {
                    desired = pickSlot(TeamState::RETREAT);
                }
                else {
                    desired = pickSlot(TeamState::DEFEND);
                }

                if (desired.first == -1) {
                    desired = FindSafePositionForWarrior(npc, reservedPositions);
                }

                if (desired.first != -1) {
                    auto offset = AcquireWarriorOffset(npc);
                    int targetX = desired.first;
                    int targetY = desired.second;

                    int offsetX = ClampInt(targetX + offset.first, 0, Map::W - 1);
                    int offsetY = ClampInt(targetY + offset.second, 0, Map::H - 1);

                    if (Map::InBounds(offsetX, offsetY) && Map::IsWalkable(offsetX, offsetY)) {
                        targetX = offsetX;
                        targetY = offsetY;
                    }
                    else if (Map::InBounds(offsetX, offsetY)) {
                        int freeX = 0;
                        int freeY = 0;
                        if (Map::FindNearestFreeTile(offsetX, offsetY, 4, freeX, freeY, npc)) {
                            targetX = freeX;
                            targetY = freeY;
                        }
                    }

                    npc->setOrderTarget(targetX, targetY);
                    reservedPositions.push_back({ targetX, targetY });
                }
                else {
                    npc->clearOrderTarget();
                }

                if (npc->getAmmo() == 0 && npc->getSupply() == 0) {
                    printf("   [WARN] %c fully depleted, ordering supply run.\n", symbol);
                    npc->setCurrentState(new GoToSupply());
                    npc->getCurrentState()->OnEnter(npc);
                    break;
                }

                // If warrior is in danger, prioritize cover
                if (security > 0.5 && npc->getHP() < 50) {
                    printf("   [WARN] %c is in danger (security=%.2f, HP=%d) - sending to cover\n", 
                        symbol, security, npc->getHP());
                    npc->setCurrentState(new GoToCover());
                } else if (teamState == TeamState::ATTACK) {
                    printf("   [INFO] Assigning GoToCombat() to %c (security=%.2f)\n", symbol, security);
                    npc->setCurrentState(new GoToCombat());
                } else if (teamState == TeamState::DEFEND || teamState == TeamState::RETREAT) {
                    printf("   [INFO] Assigning GoToCover() to %c (DEFEND/RETREAT mode)\n", symbol);
                    npc->setCurrentState(new GoToCover());
                } else {
                    npc->setCurrentState(new GoToCombat());
                }
                npc->getCurrentState()->OnEnter(npc);
            }
            break;

        case Role::Medic:
            {
        bool hasCriticalPatients = !criticalInjured.empty();
        if (forcedRetreat && !hasCriticalPatients) {
            printf("   [WARN] Medic %c under heavy fire, retreating to cover (security=%.2f)\n", symbol, security);
            npc->setCurrentState(new GoToCover());
            npc->getCurrentState()->OnEnter(npc);
            break;
        }

                if (!npc->CanTakeAssist()) {
                    printf("   [INFO] Medic %c assist limit reached (%d/%d). Returning to warehouse.\n",
                        symbol, npc->GetAssistsDone(), NPC::ASSIST_LIMIT);
                    Map::WarehouseInfo wh = Map::GetWarehouseForTeam(npc->getTeam());
                    npc->setCurrentState(new ReturnToWarehouse(wh.medX, wh.medY));
                    npc->getCurrentState()->OnEnter(npc);
                    break;
                }

                if (criticalInjured.empty()) {
                    if (npc->getSupply() == 0) {
                        printf("   [INFO] Medic %c depleted, heading to med supply.\n", symbol);
                        npc->setCurrentState(new GoToMedSupply());
                        npc->getCurrentState()->OnEnter(npc);
                    } else if (security > 0.45) {
                        npc->setCurrentState(new GoToCover());
                        npc->getCurrentState()->OnEnter(npc);
                    }
                    break;
                }

                NPC* injured = FindMostCriticalInjuredAlly();
                // Check if there are injured allies
                bool hasInjured = false;
                if (injured && injured != npc) hasInjured = true;

                if (hasInjured) {
                    printf("   [INFO] Assigning GoToHeal() to %c (injured ally detected)\n", symbol);
                    npc->setCurrentState(new GoToHeal());
                } else {
                    printf("   [INFO] No injured allies, sending %c to medical supply.\n", symbol);
                    npc->setCurrentState(new GoToMedSupply());
                }
                npc->getCurrentState()->OnEnter(npc);
            }
            break;

        case Role::Porter:
            {
                if (forcedRetreat) {
                    printf("   [WARN] Porter %c retreating from danger (security=%.2f)\n", symbol, security);
                    npc->setCurrentState(new GoToCover());
                    npc->getCurrentState()->OnEnter(npc);
                    break;
                }

                if (!npc->CanTakeAssist()) {
                    printf("   [INFO] Porter %c assist limit reached (%d/%d). Returning to warehouse.\n",
                        symbol, npc->GetAssistsDone(), NPC::ASSIST_LIMIT);
                    Map::WarehouseInfo wh = Map::GetWarehouseForTeam(npc->getTeam());
                    npc->setCurrentState(new ReturnToWarehouse(wh.ammoX, wh.ammoY));
                    npc->getCurrentState()->OnEnter(npc);
                    break;
                }

                if (npc->getSupply() == 0 && npc->getCurrentState() && typeid(*npc->getCurrentState()) != typeid(GoToSupply)) {
                    printf("   [INFO] Porter %c depleted, returning to warehouse.\n", symbol);
                    npc->setCurrentState(new GoToSupply());
                    npc->getCurrentState()->OnEnter(npc);
                    break;
                }

                NPC* ammoStarved = FindMostAmmoStarvedWarrior();
                bool needsAmmo = (ammoStarved != nullptr);

                if (needsAmmo && !HasActiveSupplyFor(ammoStarved)) {
                    printf("   [INFO] Assigning GoDeliverAmmo() to %c (warrior needs ammo)\n", symbol);
                    AssignDeliverAmmo(npc, ammoStarved, now);
                    break;
                }

                if (needsAmmo) {
                    printf("   [INFO] Porter %c: supply already en route to warrior %c, standing by at warehouse.\n",
                        symbol, ammoStarved->getSymbol());
                    npc->setCurrentState(new GoToSupply());
                    npc->getCurrentState()->OnEnter(npc);
                } else {
                    printf("   [INFO] Porter %c heading to ammo supply.\n", symbol);
                    npc->setCurrentState(new GoToSupply());
                    npc->getCurrentState()->OnEnter(npc);
                }
            }
            break;

        default:
            break;
        }
    }

    printf("[INFO] Commander %c finished assigning orders.\n", commander->getSymbol());
    lastPlannedTeamState = teamState;


}

// Handle incoming reports from soldiers in real-time
void Commander::ReceiveReport(NPC* sender, ReportType type)
{
    if (!sender || !sender->IsAlive()) return;
    
    if (!commander || !commander->IsAlive()) {
        printf("💀 Commander is dead! Warriors continue fighting independently...\n");
        return;
    }

    bool assignmentMade = false;
    
    if (type == ReportType::LOW_AMMO) {
        printf("📢 Commander %c received report: %c has LOW AMMO! (Ammo: %d)\n",
            commander->getSymbol(), sender->getSymbol(), sender->getAmmo());
        
        for (NPC* npc : team) {
            if (npc->getRole() == Role::Porter && npc->IsAlive()) {
                if (typeid(*npc->getCurrentState()) != typeid(GoDeliverAmmo)) {
                    printf("   📦 Commander assigns GoDeliverAmmo to Porter %c for %c\n", npc->getSymbol(), sender->getSymbol());
                    
                    if (npc->getCurrentState()) delete npc->getCurrentState();
                    npc->setTargetNPC(sender);
                    npc->setIsDelivering(true);
                    npc->setCurrentState(new GoDeliverAmmo());
                    npc->getCurrentState()->OnEnter(npc);
                    assignmentMade = true;
                    break;
                }
            }
        }
    }
    else if (type == ReportType::INJURED) {
        printf("📢 Commander %c received report: %c is INJURED! (HP=%d)\n",
            commander->getSymbol(), sender->getSymbol(), sender->getHP());
            
        for (NPC* npc : team) {
            if (npc->getRole() == Role::Medic && npc->IsAlive()) {
                State* currentState = npc->getCurrentState();
                bool alreadyHealing = currentState && typeid(*currentState) == typeid(GoToHeal);

                bool senderCritical = sender->getHP() <= 30;
                bool senderSupport = (sender->getRole() != Role::Warrior);

                if (!alreadyHealing || senderCritical || senderSupport) {
                    printf("   💉 Commander assigns GoToHeal to Medic %c for %c\n", 
                        npc->getSymbol(), sender->getSymbol());
                    
                    if (currentState) {
                        currentState->OnExit(npc);
                        delete currentState;
                        npc->setCurrentState(nullptr);
                    }
                    npc->setTargetNPC(sender);
                    npc->setIsDelivering(true);
                    npc->setCurrentState(new GoToHeal());
                    npc->getCurrentState()->OnEnter(npc);
                    assignmentMade = true;
                    break;
                }
            }
        }
    }
    else if (type == ReportType::ENEMY_SPOTTED) {
        printf("📢 Commander %c received report: %c spotted an ENEMY!\n",
            commander->getSymbol(), sender->getSymbol());
    }

    PlanAndAssignOrders();
}

// Commander moves to safe position using visibility map
void Commander::MoveToSafePosition()
{
    if (!commander || !commander->IsAlive()) return;
    
    double now = clock() / (double)CLOCKS_PER_SEC;
    if (lastRepositionCheck <= 0.0) {
        lastRepositionCheck = now;
    }

    int cx = (int)commander->getX();
    int cy = (int)commander->getY();
    
    // Check if commander is in danger
    double security = Map::GetSecurityValue(cy, cx, commander->getTeam());
    double dangerThreshold = 0.18;
    if (teamState == TeamState::ATTACK) dangerThreshold = 0.25;
    bool forceMove = security > dangerThreshold;
    bool periodicMove = (!forceMove && !commander->getIsMoving() && (now - lastRepositionCheck) > 6.0);

    if (forceMove || periodicMove) {
        if (forceMove) {
            printf("[WARN] Commander %c is in danger (security=%.2f), moving to safe position.\n",
                commander->getSymbol(), security);
        } else {
            printf("[INFO] Commander %c repositions to avoid clustering (security=%.2f).\n",
                commander->getSymbol(), security);
        }
        
        // Find safe cover nearby
        std::pair<int, int> safePos;
        if (Path::FindNearestCover(cx, cy, forceMove ? 30 : 20, commander->getTeam(), safePos)) {
            std::vector<std::pair<int, int>> path;
            if (Path::FindSafePath(cx, cy, safePos.first, safePos.second, commander->getTeam(), path, 0.8, commander->GetId())) {
                commander->SetPath(path);
                printf("[INFO] Commander %c moving to safe position (%d,%d)\n",
                    commander->getSymbol(), safePos.first, safePos.second);
            }
        }
        lastRepositionCheck = now;
    }
}

// Use visibility map to plan attack routes
void Commander::PlanAttackRoute()
{
    // This function uses visibility map to identify attack routes
    if (teamState == TeamState::ATTACK) {

    }
}

