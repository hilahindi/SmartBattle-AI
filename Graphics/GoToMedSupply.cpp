#include "GoToMedSupply.h"
#include "NPC.h"
#include "Map.h"
#include "GoToCover.h"
#include <stdio.h>
#include <time.h>
#include "Pathfinding.h"
#include "Definitions.h"

static double arrivalTime = 0;
static bool waitingAtMed = false;

// When Medic gets the order to go to *lower* medical warehouse
void GoToMedSupply::OnEnter(NPC* pn)
{
    printf("[STATE] [%c] heading to medical warehouse (bottom side).\n", pn->getSymbol());
    waitingAtMed = false;
    arrivalTime = 0;
    pn->setIsResting(false);

    //  get team-specific warehouse info 
    Map::WarehouseInfo wh = Map::GetWarehouseForTeam(pn->getTeam());
    int targetX = wh.medX;
    int targetY = wh.medY;

    //  force medics to choose the *bottom* warehouse 
    // (Orange = bottom-left, Blue = bottom-right)
    if (pn->getTeam() == TeamId::Orange) {
        targetX = 15;
        targetY = 15;   // bottom-left
    }
    else {
        targetX = 185;
        targetY = 15;   // bottom-right
    }

    printf("[PATH] [%c] target medical warehouse (%d,%d)\n", pn->getSymbol(), targetX, targetY);

    //  if tile not walkable, find nearby free tile 
    if (!Map::IsWalkable(targetX, targetY)) {
        const int DX[8] = { 1,-1,0,0, 1,-1, 1,-1 };
        const int DY[8] = { 0,0,1,-1, 1,-1,-1,1 };
        bool found = false;

        for (int i = 0; i < 8 && !found; ++i) {
            int nx = targetX + DX[i];
            int ny = targetY + DY[i];
            if (Map::IsWalkable(nx, ny)) {
                targetX = nx;
                targetY = ny;
                found = true;
            }
        }

        if (!found) {
            printf("[WARN] [%c] no walkable tile near medical warehouse.\n", pn->getSymbol());
            pn->setCurrentState(new GoToCover());
            pn->getCurrentState()->OnEnter(pn);
            return;
        }
    }

    //  compute safe path (prefer safer routes) 
    std::vector<std::pair<int, int>> path;
    int sx = (int)pn->getX();
    int sy = (int)pn->getY();

    if (Path::FindSafePath(sx, sy, targetX, targetY, pn->getTeam(), path, 0.6, pn->GetId())) {
        printf("[PATH] [%c] safe path to medical warehouse length=%zu\n", pn->getSymbol(), path.size());
        pn->SetPath(path);
    }
    else {
        printf("[WARN] [%c] strict safe path failed, relaxing weight.\n", pn->getSymbol());
        if (Path::FindSafePath(sx, sy, targetX, targetY, pn->getTeam(), path, 0.3, pn->GetId())) {
            pn->SetPath(path);
            printf("[PATH] [%c] relaxed safe path length=%zu\n", pn->getSymbol(), path.size());
        }
        else if (Path::FindPath(sx, sy, targetX, targetY, path, pn->GetId())) {
            pn->SetPath(path);
            printf("[PATH] [%c] fallback path length=%zu\n", pn->getSymbol(), path.size());
        }
        else {
            std::pair<int, int> fallbackCover;
            if (Path::FindNearestCover(targetX, targetY, 14, pn->getTeam(), fallbackCover) &&
                (Path::FindSafePath(sx, sy, fallbackCover.first, fallbackCover.second, pn->getTeam(), path, 0.4, pn->GetId()) ||
                    Path::FindPath(sx, sy, fallbackCover.first, fallbackCover.second, path, pn->GetId())))
            {
                pn->SetPath(path);
                printf("[PATH] [%c] rerouted to safe cover near med depot (%d,%d).\n",
                    pn->getSymbol(), fallbackCover.first, fallbackCover.second);
            }
            else {
                printf("[ERROR] [%c] no path to medical warehouse. Staying put.\n", pn->getSymbol());
                pn->setCurrentState(new GoToCover());
                pn->getCurrentState()->OnEnter(pn);
                return;
            }
        }
    }
}

// Transition - while moving or upon arrival
void GoToMedSupply::Transition(NPC* pn) {
    if (pn->getIsMoving()) return;  // still walking

    //  get bottom warehouse location again 
    int targetX = (pn->getTeam() == TeamId::Orange) ? 15 : 185;
    int targetY = 15;

    double dx = pn->getX() - targetX;
    double dy = pn->getY() - targetY;
    double dist2 = dx * dx + dy * dy;
    if (dist2 > 4.0) return; // not close enough yet

    //  start waiting once arrived 
    if (!waitingAtMed) {
        waitingAtMed = true;
        arrivalTime = clock();
        printf("[STATE] [%c] waiting at medical warehouse.\n", pn->getSymbol());
        return;
    }

    //  simulate waiting for resupply (~2s) 
    double elapsed = (clock() - arrivalTime) / CLOCKS_PER_SEC;
    if (elapsed < 2.0) return;

    //  finished collecting supplies 
    pn->setSupply(MEDIC_MAX_SUPPLIES);
    pn->ResetAssistCounter();
    pn->setIsMoving(false);
    pn->setIsResting(true);
    printf("[STATE] [%c] stocked medical supplies and is holding position.\n", pn->getSymbol());
}

// Exit from state
void GoToMedSupply::OnExit(NPC* pn) {
    pn->setIsMoving(false);
}
