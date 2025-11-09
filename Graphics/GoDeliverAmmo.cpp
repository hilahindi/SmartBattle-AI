#include "GoDeliverAmmo.h"
#include "NPC.h"
#include "Map.h"
#include "GoToCover.h"
#include "GoToSupply.h"
#include <stdio.h>
#include <cmath>
#include <ctime>
#include <limits>
#include <vector>
#include <cstdlib>
#include "Pathfinding.h"
#include "Definitions.h"
#include "ReturnToWarehouse.h"

extern std::vector<NPC*> teamOrange;
extern std::vector<NPC*> teamBlue;

namespace {

int RoundToGrid(double value)
{
    return (value >= 0.0) ? (int)(value + 0.5) : (int)(value - 0.5);
}

const std::vector<std::pair<int, int>> ORANGE_EXIT_CORRIDOR = {
    {26, 82}, {28, 80}, {30, 77}, {32, 73}
};

const std::vector<std::pair<int, int>> BLUE_EXIT_CORRIDOR = {
    {174, 82}, {172, 80}, {170, 77}, {168, 73}
};

double Distance(double ax, double ay, double bx, double by)
{
    double dx = ax - bx;
    double dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

static void PrependDepotExit(NPC* pn, std::vector<std::pair<int, int>>& path)
{
    if (!pn || path.empty()) return;
    const auto& corridor = (pn->getTeam() == TeamId::Orange) ? ORANGE_EXIT_CORRIDOR : BLUE_EXIT_CORRIDOR;
    if (corridor.empty()) return;

    double px = pn->getX();
    double py = pn->getY();
    auto dist2 = [&](double ax, double ay) {
        double dx = ax - px;
        double dy = ay - py;
        return dx * dx + dy * dy;
    };

    if (dist2(corridor.front().first, corridor.front().second) > 36.0)
        return;

    std::vector<std::pair<int, int>> refined;
    for (const auto& cell : corridor) {
        if (!Map::InBounds(cell.first, cell.second)) continue;
        if (!Map::IsWalkable(cell.first, cell.second)) continue;
        if (Map::IsOccupied(cell.first, cell.second, pn->GetId())) continue;
        refined.push_back(cell);
    }

    if (refined.empty()) return;
    if (!path.empty() && refined.back() == path.front())
        refined.pop_back();
    if (refined.empty()) return;

    refined.insert(refined.end(), path.begin(), path.end());
    path.swap(refined);
}

bool PlanPathToAlly(NPC* pn, NPC* target)
{
    if (!pn || !target) return false;

    int sx = RoundToGrid(pn->getX());
    int sy = RoundToGrid(pn->getY());
    int goalX = RoundToGrid(target->getX());
    int goalY = RoundToGrid(target->getY());

    std::vector<std::pair<int, int>> goalCandidates;
    auto collectCandidates = [&](int radius)
    {
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                if (radius > 0 && std::abs(dx) != radius && std::abs(dy) != radius) continue;
                int nx = goalX + dx;
                int ny = goalY + dy;
                if (!Map::InBounds(nx, ny)) continue;
                if (!Map::IsWalkable(nx, ny)) continue;
                if (Map::IsOccupied(nx, ny, pn->GetId())) continue;
                goalCandidates.emplace_back(nx, ny);
            }
        }
    };
    collectCandidates(1);
    if (goalCandidates.empty()) collectCandidates(2);
    if (goalCandidates.empty()) collectCandidates(3);
    goalCandidates.emplace_back(goalX, goalY); // fallback: original cell

    auto pickRandom = [&](const std::vector<std::pair<int, int>>& candidates) -> std::vector<std::pair<int, int>> {
        std::vector<std::pair<int, int>> shuffled = candidates;
        for (size_t i = 0; i < shuffled.size(); ++i) {
            size_t j = i + (rand() % (shuffled.size() - i));
            std::swap(shuffled[i], shuffled[j]);
        }
        return shuffled;
    };

    if (!Map::InBounds(goalX, goalY)) return false;

    if (!Map::IsWalkable(sx, sy)) {
        std::vector<std::pair<int, int>> exits;
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = -2; dy <= 2; ++dy) {
                int nx = sx + dx;
                int ny = sy + dy;
                if (!Map::InBounds(nx, ny)) continue;
                if (!Map::IsWalkable(nx, ny)) continue;
                if (Map::IsOccupied(nx, ny, pn->GetId())) continue;
                exits.emplace_back(nx, ny);
            }
        }
        if (!exits.empty()) {
            auto exitCell = exits[rand() % exits.size()];
            printf("[WARN] Porter %c: stepping out of warehouse via (%d,%d).\n",
                pn->getSymbol(), exitCell.first, exitCell.second);
            std::vector<std::pair<int, int>> escapePath;
            escapePath.emplace_back(exitCell.first, exitCell.second);
            pn->SetPath(escapePath);
            return true;
        }
        printf("[ERROR] Porter %c: no walkable exit around (%d,%d).\n", pn->getSymbol(), sx, sy);
        return false;
    }

    std::vector<std::pair<int, int>> path;
    bool success = false;
    auto candidates = pickRandom(goalCandidates);
    for (const auto& goal : candidates) {
        int gx = goal.first;
        int gy = goal.second;
        success =
        Path::FindSafePath(sx, sy, gx, gy, pn->getTeam(), path, 0.6, pn->GetId()) ||
        Path::FindSafePath(sx, sy, gx, gy, pn->getTeam(), path, 0.3, pn->GetId()) ||
        Path::FindPath(sx, sy, gx, gy, path, pn->GetId());
        if (success) {
            goalX = gx;
            goalY = gy;
            break;
        }
    }

    if (!success) {
        std::pair<int, int> fallbackCover;
        if (Path::FindNearestCover(goalX, goalY, 10, pn->getTeam(), fallbackCover)) {
            if (!Map::IsWalkable(fallbackCover.first, fallbackCover.second)) {
                const int DX[4] = { 1, -1, 0, 0 };
                const int DY[4] = { 0, 0, 1, -1 };
                for (int i = 0; i < 4; ++i) {
                    int nx = fallbackCover.first + DX[i];
                    int ny = fallbackCover.second + DY[i];
                    if (Map::InBounds(nx, ny) && Map::IsWalkable(nx, ny)) {
                        fallbackCover = { nx, ny };
                        break;
                    }
                }
            }
            if (Path::FindSafePath(sx, sy, fallbackCover.first, fallbackCover.second, pn->getTeam(), path, 0.4, pn->GetId()) ||
                Path::FindPath(sx, sy, fallbackCover.first, fallbackCover.second, path, pn->GetId())) {
                printf("[PATH] Porter %c rerouting near ally cover (%d,%d)\n",
                    pn->getSymbol(), fallbackCover.first, fallbackCover.second);
                pn->SetPath(path);
                return true;
            }
        }
        return false;
    }

    PrependDepotExit(pn, path);

    printf("[PATH] Porter %c safe path to ally length=%zu from (%d,%d) -> (%d,%d)\n",
        pn->getSymbol(), path.size(), sx, sy, goalX, goalY);
    pn->SetPath(path);
    return true;
}

bool FindLowAmmoAlly(NPC* pn, NPC*& outTarget)
{
    outTarget = nullptr;
    double bestDist = 1e9;

    const std::vector<NPC*>& myTeam =
        (pn->getTeam() == TeamId::Orange) ? teamOrange : teamBlue;

    for (NPC* ally : myTeam)
    {
        if (!ally || ally == pn) continue;
        if (!ally->IsAlive()) continue;
        bool needsAmmo = ally->NeedsAmmo();
        bool needsGrenades = ally->getGrenades() <= 1;
        if (!(needsAmmo || needsGrenades)) continue;

        double dx = pn->getX() - ally->getX();
        double dy = pn->getY() - ally->getY();
        double dist2 = dx * dx + dy * dy;
        if (needsGrenades && !needsAmmo) {
            dist2 *= 0.85;
        }
        if (dist2 < bestDist)
        {
            bestDist = dist2;
            outTarget = ally;
        }
    }

    return outTarget != nullptr;
}

}

void GoDeliverAmmo::OnEnter(NPC* pn)
{
    printf("[STATE] Porter %c entering GoDeliverAmmo state.\n", pn->getSymbol());
    pn->setIsResting(false);
    pn->setIsDelivering(true);
    waitingToDeliver = false;
    lastDistanceCheckTime = clock() / (double)CLOCKS_PER_SEC;
    lastDistanceToTarget = std::numeric_limits<double>::max();

    if (pn->getSupply() == 0) {
        printf("[WARN] Porter %c has no ammo crates. Redirecting to warehouse.\n", pn->getSymbol());
        OnExit(pn);
        pn->setCurrentState(new GoToSupply());
        pn->getCurrentState()->OnEnter(pn);
        return;
    }

    if (targetLowAmmo) {
        if (!targetLowAmmo->IsAlive() || !targetLowAmmo->NeedsAmmo()) {
            targetLowAmmo = nullptr;
        }
    }

    if (!targetLowAmmo && !FindLowAmmoAlly(pn, targetLowAmmo)) {
        printf("[INFO] Porter %c found no ally needing ammo. Holding position.\n", pn->getSymbol());
        OnExit(pn);
        pn->setCurrentState(new GoToCover());
        pn->getCurrentState()->OnEnter(pn);
        return;
    }

    targetX = targetLowAmmo->getX();
    targetY = targetLowAmmo->getY();
    pn->setTargetNPC(targetLowAmmo);

    printf("[PATH] Porter %c moving to low-ammo ally at (%.1f, %.1f) [Ammo=%d]\n",
        pn->getSymbol(),
        targetLowAmmo->getX(), targetLowAmmo->getY(),
        targetLowAmmo->getAmmo());

    if (!PlanPathToAlly(pn, targetLowAmmo)) {
        printf("[WARN] Porter %c: Need to step out before reaching ally. Moving to cover.\n", pn->getSymbol());
        OnExit(pn);
        pn->setCurrentState(new GoToCover());
        pn->getCurrentState()->OnEnter(pn);
        return;
    }
}

void GoDeliverAmmo::Transition(NPC* pn)
{
    if (!pn) return;

    if (!targetLowAmmo || !targetLowAmmo->IsAlive()) {
        printf("[WARN] Porter %c: Target unavailable. Standing down.\n", pn->getSymbol());
        OnExit(pn);
        pn->setCurrentState(new GoToSupply());
        pn->getCurrentState()->OnEnter(pn);
        return;
    }

    if (!targetLowAmmo->NeedsAmmo()) {
        printf("[INFO] Porter %c: Target already resupplied. Returning.\n", pn->getSymbol());
        OnExit(pn);
        pn->setCurrentState(new GoToSupply());
        pn->getCurrentState()->OnEnter(pn);
        return;
    }

    targetX = targetLowAmmo->getX();
    targetY = targetLowAmmo->getY();

    double distance = Distance(pn->getX(), pn->getY(), targetX, targetY);
    const double DELIVERY_RADIUS = 3.5;
    double now = clock() / (double)CLOCKS_PER_SEC;

    if (distance <= DELIVERY_RADIUS) {
        if (!waitingToDeliver) {
            pn->SetPath(std::vector<std::pair<int, int>>());
            pn->setIsMoving(false);
            waitingToDeliver = true;
            targetLowAmmo->RefillAmmo();
            printf("[INFO] [P] delivered ammo to [%c] at (%.1f, %.1f)\n",
                targetLowAmmo->getSymbol(), targetX, targetY);

            double assistedX = targetLowAmmo->getX();
            double assistedY = targetLowAmmo->getY();
            pn->consumeSupply(1);
            pn->RegisterAssistCompletion();
            if (!pn->CanTakeAssist()) {
                printf("[INFO] Porter %c reached assist limit (%d/%d).\n",
                    pn->getSymbol(), pn->GetAssistsDone(), NPC::ASSIST_LIMIT);
            }
            pn->setIsDelivering(false);
            OnExit(pn);
            Map::WarehouseInfo wh = Map::GetWarehouseForTeam(pn->getTeam());
            printf("[INFO] Porter %c returning to warehouse standby.\n", pn->getSymbol());
            pn->setCurrentState(new ReturnToWarehouse(wh.ammoX, wh.ammoY, 4.0, assistedX, assistedY));
            pn->getCurrentState()->OnEnter(pn);
        }
        return;
    }

    if (pn->getIsMoving()) {
        if ((now - lastDistanceCheckTime) > 1.0) {
            if (distance >= lastDistanceToTarget - 0.2) {
                printf("[WARN] Porter %c progress stalled at distance %.2f. Replanning route.\n",
                    pn->getSymbol(), distance);
                if (!PlanPathToAlly(pn, targetLowAmmo)) {
                    printf("[ERROR] Porter %c unable to find alternate path to ally. Seeking cover.\n",
                        pn->getSymbol());
                    OnExit(pn);
                    pn->setCurrentState(new GoToCover());
                    pn->getCurrentState()->OnEnter(pn);
                    return;
                }
            }
            lastDistanceToTarget = distance;
            lastDistanceCheckTime = now;
        }
        return;
    }

    if (!PlanPathToAlly(pn, targetLowAmmo)) {
        printf("[WARN] Porter %c stuck en route. Replanning failed, seeking cover.\n", pn->getSymbol());
        OnExit(pn);
        pn->setCurrentState(new GoToCover());
        pn->getCurrentState()->OnEnter(pn);
    }
}

void GoDeliverAmmo::OnExit(NPC* pn)
{
    waitingToDeliver = false;
    targetLowAmmo = nullptr;
    targetX = targetY = 0.0;
    lastDistanceCheckTime = 0.0;
    lastDistanceToTarget = 0.0;
    if (pn) {
        pn->setIsDelivering(false);
        pn->setTargetNPC(nullptr);
    }
}
