#include "ReturnToWarehouse.h"
#include "NPC.h"
#include "Map.h"
#include "Pathfinding.h"
#include "Definitions.h"
#include "GoToSupply.h"
#include "GoToMedSupply.h"
#include "GoToHeal.h"
#include <stdio.h>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <vector>
#include <limits>

extern std::vector<NPC*> teamOrange;
extern std::vector<NPC*> teamBlue;

namespace {
    constexpr double REPATH_INTERVAL = 2.0;

    double NowSeconds() {
        return clock() / static_cast<double>(CLOCKS_PER_SEC);
    }

    double RandomRange(double minValue, double maxValue) {
        if (maxValue <= minValue) return minValue;
        double t = static_cast<double>(rand()) / static_cast<double>(RAND_MAX);
        return minValue + (maxValue - minValue) * t;
    }
NPC* FindPriorityInjured(NPC* medic)
{
    const std::vector<NPC*>& myTeam =
        (medic->getTeam() == TeamId::Orange) ? teamOrange : teamBlue;

    NPC* best = nullptr;
    int bestScore = std::numeric_limits<int>::max();
    double bestDist = std::numeric_limits<double>::max();

    for (NPC* ally : myTeam) {
        if (!ally || ally == medic) continue;
        if (!ally->IsAlive()) continue;
        if (ally->getHP() >= INJURY_THRESHOLD) continue;

        int priority = ally->getHP();
        switch (ally->getRole()) {
        case Role::Warrior:   priority -= 10; break;
        case Role::Commander: priority -= 8;  break;
        case Role::Porter:    priority -= 5;  break;
        default:              break;
        }
        if (ally->getHP() <= 30) priority -= 15;

        double dx = medic->getX() - ally->getX();
        double dy = medic->getY() - ally->getY();
        double dist2 = dx * dx + dy * dy;

        if (priority < bestScore || (priority == bestScore && dist2 < bestDist)) {
            bestScore = priority;
            bestDist = dist2;
            best = ally;
        }
    }
    return best;
}
}

ReturnToWarehouse::ReturnToWarehouse(int warehouseX, int warehouseY, double radius,
    double avoidPosX, double avoidPosY)
    : centerX(warehouseX)
    , centerY(warehouseY)
    , patrolRadius(radius)
    , arrivalRadius(3.0)
    , inPatrol(false)
    , nextPatrolTime(0.0)
    , lastRepathCheck(0.0)
    , retreating(false)
    , retreatTargetX(warehouseX)
    , retreatTargetY(warehouseY)
    , hasAvoidPoint(std::isfinite(avoidPosX) && std::isfinite(avoidPosY))
    , avoidX(avoidPosX)
    , avoidY(avoidPosY)
{
}

void ReturnToWarehouse::OnEnter(NPC* pn) {
    if (!pn) return;
    inPatrol = false;
    nextPatrolTime = 0.0;
    lastRepathCheck = NowSeconds();
    retreating = false;

    pn->setIsDelivering(false);
    pn->setIsResting(false);
    pn->setIsMoving(false);

    double now = lastRepathCheck;
    if (!StartRetreat(pn, now)) {
        PlanRouteToWarehouse(pn);
        printf("[STATE] [%c] returning to warehouse hub (%d,%d).\n",
            pn->getSymbol(), centerX, centerY);
    }
    else {
        printf("[STATE] [%c] retreating from frontline before returning to warehouse.\n",
            pn->getSymbol());
    }
}

void ReturnToWarehouse::Transition(NPC* pn) {
    if (!pn) return;

    double now = NowSeconds();

    Map::WarehouseInfo wh = Map::GetWarehouseForTeam(pn->getTeam());
    double targetHubX = static_cast<double>(wh.ammoX);
    double targetHubY = static_cast<double>(wh.ammoY);
    if (pn->getRole() == Role::Medic) {
        targetHubX = static_cast<double>(wh.medX);
        targetHubY = static_cast<double>(wh.medY);
    }

    double dx = pn->getX() - targetHubX;
    double dy = pn->getY() - targetHubY;
    double dist2 = dx * dx + dy * dy;
    double arrivalRadius2 = arrivalRadius * arrivalRadius;

    if (retreating) {
        double rdx = pn->getX() - static_cast<double>(retreatTargetX);
        double rdy = pn->getY() - static_cast<double>(retreatTargetY);
        double retreatDist2 = rdx * rdx + rdy * rdy;
        if (retreatDist2 <= arrivalRadius2 || !pn->getIsMoving()) {
            retreating = false;
            PlanRouteToWarehouse(pn);
            printf("[STATE] [%c] retreat complete, heading to warehouse hub.\n", pn->getSymbol());
            return;
        }

        if ((now - lastRepathCheck) > REPATH_INTERVAL && pn->getIsMoving()) {
            lastRepathCheck = now;
            PlanPathTo(pn, retreatTargetX, retreatTargetY);
        }
        return;
    }

    if (!inPatrol) {
        if (dist2 <= arrivalRadius2) {
            if (pn->getIsMoving()) {
                pn->SetPath(std::vector<std::pair<int, int>>());
                pn->setIsMoving(false);
            }

            bool isPorter = pn->getRole() == Role::Porter;
            bool isMedic = pn->getRole() == Role::Medic;

            if (isPorter && pn->getSupply() < PORTER_MAX_SUPPLIES) {
                pn->setSupply(PORTER_MAX_SUPPLIES);
                pn->ResetAssistCounter();
                pn->setIsDelivering(false);
                pn->setLowAmmo(false);
                pn->setIsResting(true);
                printf("[STATE] [%c] auto-refilled ammo crate at warehouse (%d,%d).\n",
                    pn->getSymbol(), wh.ammoX, wh.ammoY);
            }

            if (isMedic && pn->getSupply() < MEDIC_MAX_SUPPLIES) {
                pn->setSupply(MEDIC_MAX_SUPPLIES);
                pn->ResetAssistCounter();
                pn->setIsDelivering(false);
                pn->setIsResting(true);
                printf("[STATE] [%c] auto-refilled medical supplies at warehouse.\n", pn->getSymbol());

                NPC* nextPatient = FindPriorityInjured(pn);
                if (nextPatient) {
                    pn->setTargetNPC(nextPatient);
                    pn->setCurrentState(new GoToHeal());
                    pn->getCurrentState()->OnEnter(pn);
                    return;
                }
            }

            StartPatrol(pn, now);
            return;
        }

        if (!pn->getIsMoving() && dist2 > arrivalRadius2) {
            lastRepathCheck = now;
            PlanRouteToWarehouse(pn);
            return;
        }

        if ((now - lastRepathCheck) > REPATH_INTERVAL && pn->getIsMoving()) {
            lastRepathCheck = now;
            if (dist2 > arrivalRadius2 * 4.0) {
                PlanRouteToWarehouse(pn);
            }
        }
        return;
    }

    if (pn->getIsMoving()) {
        return;
    }

    if (now >= nextPatrolTime) {
        IssuePatrolMove(pn, now);
    }
}

void ReturnToWarehouse::OnExit(NPC* pn) {
    if (!pn) return;
    pn->setIsResting(false);
}

bool ReturnToWarehouse::PlanPathTo(NPC* pn, int gx, int gy) {
    if (!pn) return false;

    int startX = static_cast<int>(std::round(pn->getX()));
    int startY = static_cast<int>(std::round(pn->getY()));

    std::vector<std::pair<int, int>> path;
    bool foundPath =
        Path::FindSafePath(startX, startY, gx, gy, pn->getTeam(), path, 0.4, pn->GetId()) ||
        Path::FindSafePath(startX, startY, gx, gy, pn->getTeam(), path, 0.2, pn->GetId()) ||
        Path::FindPath(startX, startY, gx, gy, path, pn->GetId());

    if (foundPath && !path.empty()) {
        pn->SetPath(path);
        return true;
    }
    return false;
}

void ReturnToWarehouse::PlanRouteToWarehouse(NPC* pn) {
    if (!pn) return;

    int targetX = centerX;
    int targetY = centerY;

    if (!Map::IsWalkable(targetX, targetY)) {
        int adjustedX = targetX;
        int adjustedY = targetY;
        if (Map::FindNearestFreeTile(targetX, targetY, 3, adjustedX, adjustedY, pn)) {
            targetX = adjustedX;
            targetY = adjustedY;
        }
    }

    if (!PlanPathTo(pn, targetX, targetY)) {
        pn->setIsMoving(false);
        pn->setIsResting(true);
        printf("[WARN] [%c] could not find route back to warehouse (%d,%d).\n",
            pn->getSymbol(), centerX, centerY);
    }
}

bool ReturnToWarehouse::StartRetreat(NPC* pn, double now) {
    if (!pn || !hasAvoidPoint) return false;

    double dirX = pn->getX() - avoidX;
    double dirY = pn->getY() - avoidY;
    double len = std::sqrt(dirX * dirX + dirY * dirY);

    if (len < 0.5) {
        double angle = RandomRange(0.0, 2.0 * M_PI);
        dirX = std::cos(angle);
        dirY = std::sin(angle);
        len = 1.0;
    }

    dirX /= len;
    dirY /= len;

    double retreatDistance = std::max(8.0, patrolRadius * 2.0);
    int gx = static_cast<int>(std::round(pn->getX() + dirX * retreatDistance));
    int gy = static_cast<int>(std::round(pn->getY() + dirY * retreatDistance));

    if (!Map::InBounds(gx, gy) || !Map::IsWalkable(gx, gy)) {
        int altX = gx;
        int altY = gy;
        if (!Map::FindNearestFreeTile(gx, gy, 4, altX, altY, pn)) {
            return false;
        }
        gx = altX;
        gy = altY;
    }

    if (!PlanPathTo(pn, gx, gy)) {
        return false;
    }

    retreating = true;
    retreatTargetX = gx;
    retreatTargetY = gy;
    lastRepathCheck = now;
    inPatrol = false;
    pn->setIsResting(false);
    return true;
}

void ReturnToWarehouse::StartPatrol(NPC* pn, double now) {
    if (!pn) return;
    inPatrol = true;
    nextPatrolTime = now;
    pn->setIsResting(true);
    pn->setIsMoving(false);
    printf("[STATE] [%c] arrived at warehouse hub, starting patrol.\n", pn->getSymbol());
}

void ReturnToWarehouse::IssuePatrolMove(NPC* pn, double now) {
    if (!pn) return;

    for (int attempt = 0; attempt < 5; ++attempt) {
        double angle = RandomRange(0.0, 2.0 * M_PI);
        double radius = RandomRange(patrolRadius * 0.5, patrolRadius);
        int gx = centerX + static_cast<int>(std::round(std::cos(angle) * radius));
        int gy = centerY + static_cast<int>(std::round(std::sin(angle) * radius));

        if (!Map::InBounds(gx, gy)) continue;
        if (!Map::IsWalkable(gx, gy)) {
            int altX = gx;
            int altY = gy;
            if (!Map::FindNearestFreeTile(gx, gy, 2, altX, altY, pn)) {
                continue;
            }
            gx = altX;
            gy = altY;
        }

        pn->GoToGrid(gx, gy);
        pn->setIsResting(false);
        nextPatrolTime = now + RandomRange(3.0, 5.0);
        return;
    }

    nextPatrolTime = now + 1.0;
}

