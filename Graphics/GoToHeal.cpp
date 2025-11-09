#include <limits>
#include "GoToHeal.h"
#include "NPC.h"
#include "GoToCover.h"
#include "GoToCombat.h"
#include "GoToMedSupply.h"
#include "Map.h"
#include <stdio.h>
#include <ctime>
#include <cmath>
#include "Pathfinding.h"
#include "Definitions.h"
#include "ReturnToWarehouse.h"

extern std::vector<NPC*> teamOrange;
extern std::vector<NPC*> teamBlue;

namespace {

NPC* FindInjuredAlly(NPC* medic)
{
    const std::vector<NPC*>& myTeam =
        (medic->getTeam() == TeamId::Orange) ? teamOrange : teamBlue;

    NPC* best = nullptr;
    int lowestHP = std::numeric_limits<int>::max();
    double bestDist = 1e9;
    for (NPC* ally : myTeam) {
        if (!ally || ally == medic) continue;
        if (!ally->IsAlive()) continue;
        if (ally->getHP() >= INJURY_THRESHOLD) continue;

        int priorityBias = 0;
        switch (ally->getRole()) {
        case Role::Warrior:   priorityBias = 0; break;
        case Role::Commander: priorityBias = 5; break;
        case Role::Porter:    priorityBias = 10; break;
        case Role::Medic:     priorityBias = 15; break;
        default:              priorityBias = 12; break;
        }
        if (ally->getHP() <= 35) priorityBias -= 15;

        double dx = medic->getX() - ally->getX();
        double dy = medic->getY() - ally->getY();
        double dist2 = dx * dx + dy * dy;
        int effectiveHp = ally->getHP() + priorityBias;

        if (effectiveHp < lowestHP || (effectiveHp == lowestHP && dist2 < bestDist)) {
            lowestHP = effectiveHp;
            bestDist = dist2;
            best = ally;
        }
    }
    return best;
}

bool BuildPathToTarget(NPC* medic, NPC* wounded)
{
    int sx = static_cast<int>(std::round(medic->getX()));
    int sy = static_cast<int>(std::round(medic->getY()));
    int gx = static_cast<int>(std::round(wounded->getX()));
    int gy = static_cast<int>(std::round(wounded->getY()));

    std::vector<std::pair<int, int>> path;
    bool success =
        Path::FindSafePath(sx, sy, gx, gy, medic->getTeam(), path, 0.6, medic->GetId()) ||
        Path::FindSafePath(sx, sy, gx, gy, medic->getTeam(), path, 0.3, medic->GetId()) ||
        Path::FindPath(sx, sy, gx, gy, path, medic->GetId());

    if (!success) return false;

    medic->SetPath(path);
    printf("[PATH] Medic %c path to wounded length=%zu\n", medic->getSymbol(), path.size());
    return true;
}

double Distance(double ax, double ay, double bx, double by)
{
    double dx = ax - bx;
    double dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

}

void GoToHeal::OnEnter(NPC* pn)
{
    printf("[STATE] Medic %c entering GoToHeal.\n", pn->getSymbol());
    pn->setIsResting(false);
    healing = false;
    healStart = 0.0;
    lastDistanceCheckTime = clock() / (double)CLOCKS_PER_SEC;
    lastDistanceToTarget = std::numeric_limits<double>::max();

    if (pn->getSupply() == 0) {
        printf("[WARN] Medic %c out of medkits, heading to medical supply.\n", pn->getSymbol());
        pn->setCurrentState(new GoToMedSupply());
        pn->getCurrentState()->OnEnter(pn);
        return;
    }

    targetInjured = pn->getTargetNPC();
    if (targetInjured && (!targetInjured->IsAlive() || targetInjured->getHP() >= 100)) {
        targetInjured = nullptr;
    }

    if (!targetInjured) {
        targetInjured = FindInjuredAlly(pn);
    }

    if (!targetInjured) {
        printf("[INFO] Medic %c: no injured allies.\n", pn->getSymbol());
        pn->setCurrentState(new GoToMedSupply());
        pn->getCurrentState()->OnEnter(pn);
        return;
    }

    pn->setTargetNPC(targetInjured);

    if (!BuildPathToTarget(pn, targetInjured)) {
        printf("[WARN] Medic %c cannot reach wounded ally, moving to cover.\n", pn->getSymbol());
        pn->setCurrentState(new GoToCover());
        pn->getCurrentState()->OnEnter(pn);
        return;
    }
}

void GoToHeal::Transition(NPC* pn)
{
    if (!pn) return;

    if (!targetInjured || !targetInjured->IsAlive()) {
        printf("[WARN] Medic %c: target lost.\n", pn->getSymbol());
        targetInjured = FindInjuredAlly(pn);
        if (targetInjured && BuildPathToTarget(pn, targetInjured)) {
            pn->setTargetNPC(targetInjured);
        } else {
            pn->setCurrentState(new GoToMedSupply());
            pn->getCurrentState()->OnEnter(pn);
        }
        return;
    }

    double distance = Distance(pn->getX(), pn->getY(), targetInjured->getX(), targetInjured->getY());
    const double HEAL_RADIUS = 4.0;
    const double REPLAN_INTERVAL = 0.5;

    if (distance > HEAL_RADIUS) {
        double now = clock() / (double)CLOCKS_PER_SEC;
        if (pn->getIsMoving()) {
            if ((now - lastDistanceCheckTime) > REPLAN_INTERVAL) {
                if (distance >= lastDistanceToTarget - 0.2) {
                    printf("[WARN] Medic %c progress stalled at distance %.2f. Replanning path to wounded.\n",
                        pn->getSymbol(), distance);
                    if (!BuildPathToTarget(pn, targetInjured)) {
                        pn->setCurrentState(new GoToCover());
                        pn->getCurrentState()->OnEnter(pn);
                        return;
                    }
                }
                lastDistanceToTarget = distance;
                lastDistanceCheckTime = now;
            }
        }
        else {
            if (!BuildPathToTarget(pn, targetInjured)) {
                pn->setCurrentState(new GoToCover());
                pn->getCurrentState()->OnEnter(pn);
            }
        }
        healing = false;
        return;
    }

    if (pn->getIsMoving()) {
        pn->SetPath(std::vector<std::pair<int, int>>());
        pn->setIsMoving(false);
    }

    if (targetInjured->getHP() >= 100) {
        printf("[INFO] Medic %c: ally already at full health.\n", pn->getSymbol());
        targetInjured = FindInjuredAlly(pn);
        if (targetInjured && BuildPathToTarget(pn, targetInjured)) {
            pn->setTargetNPC(targetInjured);
        } else {
            OnExit(pn);
            pn->setCurrentState(new GoToMedSupply());
            pn->getCurrentState()->OnEnter(pn);
        }
        return;
    }

    if (!healing) {
        healing = true;
        healStart = static_cast<double>(std::clock());
        printf("[STATE] Medic %c treating ally %c.\n",
            pn->getSymbol(), targetInjured->getSymbol());
        return;
    }

    double elapsed = (static_cast<double>(std::clock()) - healStart) / static_cast<double>(CLOCKS_PER_SEC);
    if (elapsed < 0.6) return;

    int newHP = std::min(100, targetInjured->getHP() + MEDIC_HEAL_AMOUNT);
    targetInjured->setHP(newHP);
    printf("[HEAL] Medic %c healed ally %c to %d HP.\n",
        pn->getSymbol(), targetInjured->getSymbol(), targetInjured->getHP());

    pn->consumeSupply(1);
    pn->RegisterAssistCompletion();
    if (!pn->CanTakeAssist()) {
        printf("[INFO] Medic %c reached assist limit (%d/%d).\n",
            pn->getSymbol(), pn->GetAssistsDone(), NPC::ASSIST_LIMIT);
    }
    targetInjured->setCurrentState(new GoToCombat());
    targetInjured->getCurrentState()->OnEnter(targetInjured);

    double patientX = targetInjured->getX();
    double patientY = targetInjured->getY();
    OnExit(pn);
    Map::WarehouseInfo wh = Map::GetWarehouseForTeam(pn->getTeam());
    pn->setCurrentState(new ReturnToWarehouse(wh.medX, wh.medY, 4.0, patientX, patientY));
    pn->getCurrentState()->OnEnter(pn);
}

void GoToHeal::OnExit(NPC* pn)
{
    (void)pn;
    healing = false;
    targetInjured = nullptr;
    lastDistanceCheckTime = 0.0;
    lastDistanceToTarget = 0.0;
}
 
