#include "GoToSupply.h"
#include "NPC.h"
#include "GoToCover.h"
#include "Map.h"
#include "Pathfinding.h"
#include <stdio.h>
#include <time.h>
#include "Definitions.h"

extern std::vector<NPC*> teamOrange;
extern std::vector<NPC*> teamBlue;

static double arrivalTime = 0;
static bool waitingAtSupply = false;

const std::vector<std::pair<int, int>> ORANGE_EXIT_CORRIDOR = {
    {26, 82}, {28, 80}, {30, 77}, {32, 73}
};

const std::vector<std::pair<int, int>> BLUE_EXIT_CORRIDOR = {
    {174, 82}, {172, 80}, {170, 77}, {168, 73}
};

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

    auto roundToInt = [](double value) -> int {
        return (value >= 0.0) ? (int)(value + 0.5) : (int)(value - 0.5);
    };

    int startX = roundToInt(px);
    int startY = roundToInt(py);
    int cx = startX;
    int cy = startY;
    bool corridorValid = true;

    std::vector<std::pair<int, int>> expanded;
    expanded.reserve(refined.size() + path.size());

    auto stepTo = [&](int nx, int ny) -> bool {
        if (!Map::InBounds(nx, ny)) {
            corridorValid = false;
            return false;
        }
        if (!Map::IsWalkable(nx, ny)) {
            corridorValid = false;
            return false;
        }
        if (!expanded.empty() && expanded.back().first == nx && expanded.back().second == ny) {
            cx = nx;
            cy = ny;
            return true;
        }
        expanded.emplace_back(nx, ny);
        cx = nx;
        cy = ny;
        return true;
    };

    for (const auto& cell : refined) {
        int tx = cell.first;
        int ty = cell.second;

        while (cx != tx && corridorValid) {
            int stepX = (tx > cx) ? 1 : -1;
            int nx = cx + stepX;
            if (!stepTo(nx, cy)) break;
        }
        if (!corridorValid) break;

        while (cy != ty && corridorValid) {
            int stepY = (ty > cy) ? 1 : -1;
            int ny = cy + stepY;
            if (!stepTo(cx, ny)) break;
        }
        if (!corridorValid) break;
    }

    if (!corridorValid || expanded.empty()) {
        return;
    }

    if (!path.empty() && expanded.back() == path.front()) {
        path.erase(path.begin());
    }

    expanded.insert(expanded.end(), path.begin(), path.end());
    path.swap(expanded);
}

// When the porter gets an order to resupply
void GoToSupply::OnEnter(NPC* pn)
{
    printf("[STATE] [%c] heading to ammo supply warehouse.\n", pn->getSymbol());

    //  Get team warehouse location 
    pn->setIsResting(false);
    Map::WarehouseInfo wh = Map::GetWarehouseForTeam(pn->getTeam());
    int targetX = wh.ammoX;
    int targetY = wh.ammoY;
    auto roundToGrid = [](double value) -> int {
        return (value >= 0.0) ? (int)(value + 0.5) : (int)(value - 0.5);
    };
    int startX = roundToGrid(pn->getX());
    int startY = roundToGrid(pn->getY());

    //  If the warehouse cell is not walkable, find nearby one 
    if (!Map::IsWalkable(targetX, targetY)) {
        const int DX[4] = { +1, -1, 0, 0 };
        const int DY[4] = { 0, 0, +1, -1 };
        bool found = false;

        for (int i = 0; i < 4 && !found; ++i) {
            int nx = targetX + DX[i];
            int ny = targetY + DY[i];
            if (Map::IsWalkable(nx, ny)) {
                targetX = nx;
                targetY = ny;
                found = true;
            }
        }

        if (!found)
            printf("[WARN] [%c] could not find walkable tile near warehouse.\n", pn->getSymbol());
    }

    printf("[PATH] [%c] target supply node (%d,%d) walkable=%d\n",
        pn->getSymbol(), targetX, targetY, Map::IsWalkable(targetX, targetY));

    //  Compute safe A* path (prefer safer routes) 
    std::vector<std::pair<int, int>> path;
    int sx = (int)startX;
    int sy = (int)startY;

    if (!Map::IsWalkable(sx, sy)) {
        bool foundAdjacent = false;
        const int OFFSETS[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (auto& offset : OFFSETS) {
            int ax = sx + offset[0];
            int ay = sy + offset[1];
            if (Map::InBounds(ax, ay) && Map::IsWalkable(ax, ay)) {
                sx = ax;
                sy = ay;
                foundAdjacent = true;
                break;
            }
        }
        if (!foundAdjacent) {
            printf("[ERROR] [%c] start position blocked; searching for nearby cover.\n", pn->getSymbol());
            std::pair<int, int> altCover;
            if (Path::FindNearestCover((int)startX, (int)startY, 8, pn->getTeam(), altCover)) {
                pn->GoToGrid(altCover.first, altCover.second);
            }
            else {
                pn->setIsMoving(false);
            }
            waitingAtSupply = false;
            arrivalTime = 0;
            return;
        }
    }

    bool foundPath = false;

    if (Path::FindSafePath(sx, sy, targetX, targetY, pn->getTeam(), path, 0.5, pn->GetId())) {
        PrependDepotExit(pn, path);
        foundPath = true;
        printf("[PATH] [%c] safe path length=%zu\n", pn->getSymbol(), path.size());
    }
    else if (Path::FindSafePath(sx, sy, targetX, targetY, pn->getTeam(), path, 0.2, pn->GetId())) {
        PrependDepotExit(pn, path);
        foundPath = true;
        printf("[PATH] [%c] relaxed safe path length=%zu\n", pn->getSymbol(), path.size());
    }
    else if (Path::FindPath(sx, sy, targetX, targetY, path, pn->GetId())) {
        PrependDepotExit(pn, path);
        foundPath = true;
        printf("[PATH] [%c] fallback A* path length=%zu\n", pn->getSymbol(), path.size());
    }
    else {
        std::pair<int, int> fallbackCover;
        if (Path::FindNearestCover(targetX, targetY, 12, pn->getTeam(), fallbackCover)) {
            if (!Map::IsWalkable(fallbackCover.first, fallbackCover.second)) {
                const int DX[4] = {1,-1,0,0};
                const int DY[4] = {0,0,1,-1};
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
                PrependDepotExit(pn, path);
                foundPath = true;
                printf("[PATH] [%c] rerouted to nearby cover (%d,%d).\n",
                    pn->getSymbol(), fallbackCover.first, fallbackCover.second);
            }
        }
    }

    if (foundPath) {
        pn->SetPath(path);
    }
    else {
        printf("[ERROR] [%c] no safe route to supply warehouse. Falling back to cover.\n", pn->getSymbol());
        pn->setCurrentState(new GoToCover());
        pn->getCurrentState()->OnEnter(pn);
        waitingAtSupply = false;
        arrivalTime = 0;
        return;
    }

    waitingAtSupply = false;
    arrivalTime = 0;
}

// While moving or waiting at supply

void GoToSupply::Transition(NPC* pn)
{
    //  If still moving 
    if (pn->getIsMoving()) return;

    //  When arrived near the warehouse 
    Map::WarehouseInfo wh = Map::GetWarehouseForTeam(pn->getTeam());
    int targetX = wh.ammoX;
    int targetY = wh.ammoY;

    double dx = pn->getX() - targetX;
    double dy = pn->getY() - targetY;
    double dist2 = dx * dx + dy * dy;

    if (dist2 > 9.0) return; // not close enough yet (3 cells radius)

    if (!waitingAtSupply) {
        waitingAtSupply = true;
        arrivalTime = clock();
        printf("[STATE] [%c] refilling ammo at warehouse.\n", pn->getSymbol());
        return;
    }

    //  Wait ~1 second 
    double elapsed = (clock() - arrivalTime) / CLOCKS_PER_SEC;
    if (elapsed < 1.0) return;

    //  Done refilling 
    pn->setSupply(PORTER_MAX_SUPPLIES);
    pn->setLowAmmo(false);
    pn->ResetAssistCounter();
    pn->setIsMoving(false);
    pn->setIsResting(true);
    printf("[STATE] [%c] refilled supply crates and is standing by.\n", pn->getSymbol());
}

// OnExit
void GoToSupply::OnExit(NPC* pn)
{
    pn->setIsMoving(false);
}
