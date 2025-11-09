#include "GoToCover.h"
#include "Map.h"
#include "Pathfinding.h"
#include <stdio.h>
#include <math.h>
#include <algorithm> // for std::min
#include <time.h>
#include <vector>
#include <cstdlib>

extern std::vector<NPC*> teamOrange;
extern std::vector<NPC*> teamBlue;

namespace {
    std::pair<int, int> GetDefensiveBandAnchor(TeamId team)
    {
        if (team == TeamId::Orange) {
            return { 95, 52 };
        }
        return { 105, 48 };
    }

    bool FindUnoccupiedSpotNear(int baseX, int baseY, NPC* self, int& outX, int& outY)
    {
        if (!Map::InBounds(baseX, baseY))
            return false;

        const int MAX_RADIUS = 3;
        std::vector<std::pair<int, int>> candidates;
        candidates.emplace_back(baseX, baseY);

        for (int radius = 1; radius <= MAX_RADIUS; ++radius) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) != radius && std::abs(dy) != radius) continue;
                    candidates.emplace_back(baseX + dx, baseY + dy);
                }
            }
        }

        for (size_t i = 0; i < candidates.size(); ++i) {
            size_t j = i + (rand() % (candidates.size() - i));
            std::swap(candidates[i], candidates[j]);
        }

        for (auto& candidate : candidates) {
            int nx = candidate.first;
            int ny = candidate.second;
            if (!Map::InBounds(nx, ny)) continue;
            if (!Map::IsWalkable(nx, ny)) continue;
            if (Map::IsOccupied(nx, ny, self->GetId())) continue;
            outX = nx;
            outY = ny;
            return true;
        }

        return false;
    }
}

// Generic cover behavior: use BFS to find safe cover point
void GoToCover::OnEnter(NPC* pn) {
    if (pn->getIsMoving()) return;

    printf("[STATE] %c searching for safe cover using BFS.\n", pn->getSymbol());

    int sx = (int)pn->getX();
    int sy = (int)pn->getY();

   
    int searchRadius = 30;
    double now = clock() / (double)CLOCKS_PER_SEC;

    if ((now - pn->GetLastRetreatTime()) < 3.0) {
        searchRadius = 60; 
        printf("[INFO] %c Forcing wider search radius (%d) due to recent thrashing/retreat.\n", pn->getSymbol(), searchRadius);
    }

    if (pn->getHasOrderTarget()) {
        auto anchor = pn->getOrderTarget();
        int targetX = anchor.first;
        int targetY = anchor.second;
        if (Map::InBounds(targetX, targetY)) {
            int adjustedX = targetX;
            int adjustedY = targetY;
            if (!FindUnoccupiedSpotNear(targetX, targetY, pn, adjustedX, adjustedY)) {
                printf("[WARN] %c ordered anchor (%d,%d) occupied. Aborting cover move.\n",
                    pn->getSymbol(), targetX, targetY);
                return;
            }
            if (adjustedX != targetX || adjustedY != targetY) {
                printf("[INFO] %c adjusting ordered anchor to (%d,%d)\n",
                    pn->getSymbol(), adjustedX, adjustedY);
                targetX = adjustedX;
                targetY = adjustedY;
            }
            if (!Map::IsWalkable(targetX, targetY)) {
                const int DX[8] = { 1,-1,0,0, 1,-1, 1,-1 };
                const int DY[8] = { 0,0,1,-1, 1,-1,-1,1 };
                for (int i = 0; i < 8; ++i) {
                    int nx = targetX + DX[i];
                    int ny = targetY + DY[i];
                    if (Map::InBounds(nx, ny) && Map::IsWalkable(nx, ny)) {
                        targetX = nx;
                        targetY = ny;
                        break;
                    }
                }
            }
            std::vector<std::pair<int, int>> path;
            if (Path::FindSafePath(sx, sy, targetX, targetY, pn->getTeam(), path, 0.8, pn->GetId())) {
                pn->SetPath(path);
                printf("[INFO] %c moving to ordered cover (%d,%d)\n",
                    pn->getSymbol(), targetX, targetY);
                return;
            }
        }
    }

    // BFS 
    std::pair<int, int> coverPoint;

    if (Path::FindNearestCover(sx, sy, searchRadius, pn->getTeam(), coverPoint)) 
    {
        printf("[INFO] %c found safe cover at (%d,%d)\n",
            pn->getSymbol(), coverPoint.first, coverPoint.second);

        int adjustedX = coverPoint.first;
        int adjustedY = coverPoint.second;
        if (!FindUnoccupiedSpotNear(coverPoint.first, coverPoint.second, pn, adjustedX, adjustedY)) {
            printf("[WARN] %c cover at (%d,%d) fully occupied. Staying put momentarily.\n",
                pn->getSymbol(), coverPoint.first, coverPoint.second);
            return;
        }
        if (adjustedX != coverPoint.first || adjustedY != coverPoint.second) {
            printf("[INFO] %c nudging cover target to free spot at (%d,%d)\n",
                pn->getSymbol(), adjustedX, adjustedY);
        }

		// A* to cover point
        std::vector<std::pair<int, int>> path;
        if (Path::FindSafePath(sx, sy, adjustedX, adjustedY, pn->getTeam(), path, 0.7, pn->GetId()))
        {
            pn->SetPath(path);
            printf("[PATH] %c safe path to cover length=%zu\n", pn->getSymbol(), path.size());
        }
        else
        {
            printf("[WARN] %c could not find safe path to cover. Remaining in place.\n", pn->getSymbol());
        }
    }
    else
    {
        printf("[WARN] %c found no safe cover within radius %d. Trying fallback.\n",
            pn->getSymbol(), searchRadius);

        std::vector<std::pair<int, int>> potentialCover = {
            {70,73}, {76,78}, {76,38}, {86,42}, {92,36},
            {104,58}, {110,62}, {112,68}, {118,74}, {124,80}, {140,28}, {148,26},
            {164,70}, {170,74},
            {45, 65}, {51, 60}, {102, 40}, {118, 32}, {132, 50}, {150, 64}, {172, 48}
        };

        double bestDist2 = 1e9;
        int bestX = -1, bestY = -1;

        for (auto& t : potentialCover)
        {
            if (Map::IsWalkable(t.first, t.second) || Map::Get(t.first, t.second) == Map::ROCK)
            {
                int targetX = t.first;
                int targetY = t.second;

                if (Map::Get(t.first, t.second) == Map::ROCK) {
                    bool foundWalkableSpot = false;
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            if (Map::IsWalkable(t.first + dx, t.second + dy)) {
                                targetX = t.first + dx;
                                targetY = t.second + dy;
                                foundWalkableSpot = true;
                                break;
                            }
                        }
                        if (foundWalkableSpot) break;
                    }
                    if (!foundWalkableSpot) continue;
                }

                double dx = targetX - pn->getX();
                double dy = targetY - pn->getY();
                double d2 = dx * dx + dy * dy;

                if (d2 < bestDist2)
                {
                    bestDist2 = d2;
                    bestX = targetX;
                    bestY = targetY;
                }
            }
        }

        if (bestX != -1)
        {
            printf("[INFO] %c using fallback cover at (%d,%d)\n",
                pn->getSymbol(), bestX, bestY);
            int adjustedX = bestX;
            int adjustedY = bestY;
            if (!FindUnoccupiedSpotNear(bestX, bestY, pn, adjustedX, adjustedY)) {
                printf("[ERROR] %c failed to find free spot near fallback cover. Remaining stationary.\n", pn->getSymbol());
                return;
            }
            pn->GoToGrid(adjustedX, adjustedY);
        }
        else {
            printf("[ERROR] %c failed to find cover. Remaining stationary.\n", pn->getSymbol());
        }
    }
}

// Once arrived to cover: wait and periodically check for new threats
void GoToCover::Transition(NPC* pn) {
    if (pn->getIsMoving()) return;


    if (pn->getIsDelivering() || pn->getIsEngaging()) return;

    int px = (int)pn->getX();
    int py = (int)pn->getY();
    double security = Map::GetSecurityValue(py, px, pn->getTeam());

    pn->setIsResting(true); 

    if (security > 0.6) {
  
        printf("[WARN] %c cover position unsafe (security=%.2f). Seeking new cover.\n",
            pn->getSymbol(), security);
        pn->setIsResting(false);
        OnEnter(pn);  
    }
    else {
        double now = clock() / (double)CLOCKS_PER_SEC;
        if (pn->getRole() == Role::Warrior &&
            pn->getPathSize() == 0 &&
            (now - pn->GetLastRetreatTime()) > 2.5 &&
            (now - pn->GetLastIdleAnchorTime()) > 6.0)
        {
            auto anchor = GetDefensiveBandAnchor(pn->getTeam());
            std::pair<int, int> coverPoint;
            if (Path::FindNearestCover(anchor.first, anchor.second, 18, pn->getTeam(), coverPoint))
            {
                double dx = coverPoint.first - pn->getX();
                double dy = coverPoint.second - pn->getY();
                if (dx * dx + dy * dy > 9.0) {
                    pn->setOrderTarget(coverPoint.first, coverPoint.second);
                    pn->MarkIdleAnchorIssued();
                    pn->setIsResting(false);
                    OnEnter(pn);
                    pn->clearOrderTarget();
                }
            }
        }
    }
}


void GoToCover::OnExit(NPC* pn) {
    pn->setIsMoving(false);
    pn->setIsResting(false);
    printf("[STATE] %c exited GoToCover state.\n", pn->getSymbol());
}