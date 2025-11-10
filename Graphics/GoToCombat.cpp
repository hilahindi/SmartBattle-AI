#include <time.h>
#include <stdio.h>
#include <cmath>
#include <typeinfo>
#include "GoToCombat.h"
#include "NPC.h"
#include "GoToCover.h"   // go to cover after fight
#include "GoToSupply.h"  // if needs ammo
#include "Pathfinding.h"
#include "Map.h"
#include "Commander.h"   // for reports
#include "Definitions.h"

// Global access to both teams
extern std::vector<NPC*> teamOrange;
extern std::vector<NPC*> teamBlue;

static double lastShotTime = 0;
static double combatStartTime = 0;
static Role lastThrowerRole = Role::Commander;
static void ExitCombatState(NPC* pn)
{
    if (!pn) return;
    pn->setIsEngaging(false);
    printf("[STATE] [%c] exited combat state.\n", pn->getSymbol());
}

static bool AlliesWithinRadius(const std::vector<NPC*>& allies, const NPC* self, double px, double py, double radiusSq)
{
    for (NPC* ally : allies) {
        if (!ally || ally == self || !ally->IsAlive()) continue;
        double dx = ally->getX() - px;
        double dy = ally->getY() - py;
        if ((dx * dx + dy * dy) <= radiusSq) {
            return true;
        }
    }
    return false;
}

static bool ClearAllyLineOfFire(NPC* shooter, NPC* target, const std::vector<NPC*>& allies)
{
    double sx = shooter->getX();
    double sy = shooter->getY();
    double tx = target->getX();
    double ty = target->getY();
    double dirX = tx - sx;
    double dirY = ty - sy;
    double targetDistSq = dirX * dirX + dirY * dirY;
    if (targetDistSq < 1e-6) return false;

    double invLenSq = 1.0 / targetDistSq;

    for (NPC* ally : allies) {
        if (!ally || ally == shooter || ally == target || !ally->IsAlive()) continue;

        double ax = ally->getX() - sx;
        double ay = ally->getY() - sy;
        double proj = (ax * dirX + ay * dirY) * invLenSq;
        if (proj <= 0.0 || proj >= 1.0) continue;

        double closestX = sx + proj * dirX;
        double closestY = sy + proj * dirY;
        double distX = ally->getX() - closestX;
        double distY = ally->getY() - closestY;
        double distSq = distX * distX + distY * distY;

        double safeRadius = std::max(0.6, shooter->getSize() * 0.4);
        if (distSq <= safeRadius * safeRadius) {
            return false;
        }
    }

    return true;
}

// When a Warrior enters combat
void GoToCombat::OnEnter(NPC* pn)
{
    if (pn->getRole() != Role::Warrior)
    {
        printf("[WARN] [%c] cannot start combat because role is not warrior.\n", pn->getSymbol());
        return;
    }

    pn->setIsEngaging(true);
    combatStartTime = clock();
    lastShotTime = 0;

    int sx = (int)pn->getX();
    int sy = (int)pn->getY();
    TeamId teamId = pn->getTeam();
    int targetX, targetY;


    if (pn->getHasOrderTarget()) {
        auto order = pn->getOrderTarget();
        targetX = order.first;
        targetY = order.second;
    }
    else {
        if (pn->getTeam() == TeamId::Orange) {
			targetX = 140;  // to blue base direction
            targetY = 60;
        }
        else {
			targetX = 60;   // to orange base direction
            targetY = 40;
        }
    }


    const int searchRadius = 18;
    int bestAmdaX = -1, bestAmdaY = -1;
    double bestScore = 9999.0;

    for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
        for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
            int currentX = sx + dx;
            int currentY = sy + dy;

            if (!Map::InBounds(currentX, currentY) || !Map::IsWalkable(currentX, currentY)) continue;

            double distToTarget = std::sqrt(std::pow(currentX - targetX, 2) + std::pow(currentY - targetY, 2));
            if (distToTarget > 35) continue;

			// skip high-risk cells
            double currentRisk = Map::GetSecurityValue(currentY, currentX, teamId);
            if (currentRisk >= 0.85) continue;

            bool hasNearbyCover = false;
            for (int oy = -2; oy <= 2 && !hasNearbyCover; ++oy) {
                for (int ox = -2; ox <= 2; ++ox) {
                    int nx = currentX + ox;
                    int ny = currentY + oy;
                    if (!Map::InBounds(nx, ny)) continue;
                    Map::Cell c = Map::Get(nx, ny);
                    if (c == Map::TREE || c == Map::ROCK || c == Map::WAREHOUSE) {
                        hasNearbyCover = true;
                        break;
                    }
                }
            }

            double distSelf = std::sqrt((currentX - sx) * (currentX - sx) + (currentY - sy) * (currentY - sy));
            double coverBonus = hasNearbyCover ? -0.15 : 0.0;
            double score = currentRisk * 12.0 + distToTarget * 0.2 + distSelf * 0.05 + coverBonus;

            if (score < bestScore) {
                bestScore = score;
                bestAmdaX = currentX;
                bestAmdaY = currentY;
            }
        }
    }

    if (bestAmdaX == -1) {
        bestAmdaX = targetX;
        bestAmdaY = targetY;
        printf("[WARN] [%c] No ideal firing position found, targeting zone (%d,%d).\n", pn->getSymbol(), bestAmdaX, bestAmdaY);
    }
    else {
        double finalRisk = Map::GetSecurityValue(bestAmdaY, bestAmdaX, teamId);
        printf("[INFO] [%c] Selected firing position (%d,%d) risk=%.2f\n", pn->getSymbol(), bestAmdaX, bestAmdaY, finalRisk);
    }

	// Plan path to the selected position
    std::vector<std::pair<int, int>> path;

    if (Path::FindSafePath(sx, sy, bestAmdaX, bestAmdaY, teamId, path, 0.9, pn->GetId()))
    {
        pn->SetPath(path);
        printf("[PATH] [%c] Safe path length=%zu\n", pn->getSymbol(), path.size());
    }
    else
    {
        printf("[WARN] [%c] Could not find strict safe path, relaxing weight.\n", pn->getSymbol());
        if (Path::FindSafePath(sx, sy, bestAmdaX, bestAmdaY, teamId, path, 0.6, pn->GetId())) {
            pn->SetPath(path);
            printf("[PATH] [%c] Safe path (relaxed) length=%zu\n", pn->getSymbol(), path.size());
        }
        else if (Path::FindPath(sx, sy, bestAmdaX, bestAmdaY, path, pn->GetId())) {
            pn->SetPath(path);
            printf("[PATH] [%c] Using fallback A* path length=%zu\n", pn->getSymbol(), path.size());
        }
        else
        {
            printf("[ERROR] [%c] Could not find path to target. Switching to cover.\n", pn->getSymbol());
            pn->clearOrderTarget();
            pn->setCurrentState(new GoToCover());
            pn->getCurrentState()->OnEnter(pn);
            return;
        }
    }

    pn->clearOrderTarget();
}

// Combat loop
void GoToCombat::Transition(NPC* pn)
{
    if (pn->getRole() != Role::Warrior || !pn->IsAlive())
        return;

    double now = clock() / CLOCKS_PER_SEC;

    //  1) Look for enemies and decide primary/secondary targets ===
    std::vector<NPC*>& enemies =
        (pn->getTeam() == TeamId::Orange) ? teamBlue : teamOrange;
    const std::vector<NPC*>& allies =
        (pn->getTeam() == TeamId::Orange) ? teamOrange : teamBlue;

    const double closeRange2 = 100.0;   // radius 10 cells
    const double dangerRange2 = 36.0;   // radius 6 cells
    int enemiesClose = 0;
    int enemiesPressing = 0;
    double closestEnemyDist2 = 1e9;
    NPC* nearestEnemy = nullptr;

    NPC* primaryTarget = nullptr;   // prefer enemy warriors/commanders for direct fire
    NPC* fallbackTarget = nullptr;  // any enemy we can hurt if no priority target visible
    NPC* grenadeTarget = nullptr;   // enemy in grenade range but not visible (warriors only)
    for (NPC* enemy : enemies)
    {
        if (!enemy || !enemy->IsAlive()) continue;

        bool enemyIsWarrior = (enemy->getRole() == Role::Warrior);
        bool enemyIsCommander = (enemy->getRole() == Role::Commander);
        double dist2 = (enemy->getX() - pn->getX()) * (enemy->getX() - pn->getX()) +
            (enemy->getY() - pn->getY()) * (enemy->getY() - pn->getY());
        bool inFireRange = pn->InRange(enemy, FIRE_RANGE);
        bool visible = pn->CanSee(enemy);

        if (dist2 < closestEnemyDist2) {
            closestEnemyDist2 = dist2;
            nearestEnemy = enemy;
        }
        if (dist2 <= closeRange2) enemiesClose++;
        if (dist2 <= dangerRange2) enemiesPressing++;

        if (enemyIsWarrior || enemyIsCommander) {
            if (visible && inFireRange) {
                primaryTarget = enemy;
                pn->ReportEnemySpotted((int)enemy->getX(), (int)enemy->getY());
                break; // focus on the first warrior we can shoot
            }
            if (pn->getRole() == Role::Warrior && !grenadeTarget && dist2 <= GRENADE_RANGE * GRENADE_RANGE) {
                grenadeTarget = enemy;
            }
        }
        else {
            if (visible && inFireRange && !fallbackTarget) {
                fallbackTarget = enemy;
            }
            if (pn->getRole() == Role::Warrior && !grenadeTarget && dist2 <= GRENADE_RANGE * GRENADE_RANGE) {
                grenadeTarget = enemy;
            }
        }
    }

    if (!primaryTarget && fallbackTarget) {
        primaryTarget = fallbackTarget;  // no warrior/commander visible, shoot what we can
    }

    int alliesClose = 0;
    for (NPC* ally : allies) {
        if (!ally || ally == pn || !ally->IsAlive()) continue;
        double dx = ally->getX() - pn->getX();
        double dy = ally->getY() - pn->getY();
        double dist2 = dx * dx + dy * dy;
        if (dist2 <= closeRange2) alliesClose++;
    }

    constexpr int CRITICAL_HP_THRESHOLD = 25;
    bool lowHealth = pn->getHP() < 40;
    bool enemyOnTop = closestEnemyDist2 <= dangerRange2;
    bool overwhelmed = enemiesClose > alliesClose + 1 || enemiesPressing >= 2 || enemyOnTop;
    bool recentlyRetreated = (now - pn->GetLastRetreatTime()) < 2.0;
    double currentRisk = Map::GetSecurityValue((int)pn->getY(), (int)pn->getX(), pn->getTeam());
    bool enemyWarriorsAlive = false;
    for (NPC* enemy : enemies) {
        if (enemy && enemy->IsAlive() && enemy->getRole() == Role::Warrior) {
            enemyWarriorsAlive = true;
            break;
        }
    }

    if (pn->getHP() <= CRITICAL_HP_THRESHOLD) {
        pn->ReportInjury();
        pn->setIsMoving(false);
        if (!recentlyRetreated) {
            printf("[COMBAT] [%c] critically injured (HP=%d). Ceasing fire and retreating for medic support.\n",
                pn->getSymbol(), pn->getHP());
            ExitCombatState(pn);
            pn->setCurrentState(new GoToCover());
            pn->MarkRetreat();
            pn->getCurrentState()->OnEnter(pn);
        }
        return;
    }

    if (!recentlyRetreated && (lowHealth || overwhelmed)) {
        pn->ReportInjury();
        printf("[COMBAT] [%c] retreating (HP=%d closeEnemies=%d allies=%d).\n",
            pn->getSymbol(), pn->getHP(), enemiesClose, alliesClose);
        State* current = pn->getCurrentState();
        bool alreadyCover = current && typeid(*current) == typeid(GoToCover);
        if (!alreadyCover) {
            ExitCombatState(pn);
            pn->setCurrentState(new GoToCover());
            pn->MarkRetreat();
            pn->getCurrentState()->OnEnter(pn);
        }
        return;
    }

    if (!pn->getIsMoving() && !primaryTarget && currentRisk > 0.55 && enemyWarriorsAlive) {
        printf("[COMBAT] [%c] area too dangerous (risk=%.2f). Seeking cover.\n",
            pn->getSymbol(), currentRisk);
        OnExit(pn);
        pn->setCurrentState(new GoToCover());
        pn->MarkRetreat();
        pn->getCurrentState()->OnEnter(pn);
        return;
    }

    // 2) Engage enemy if seen ===
    if (primaryTarget)
    {
        if (pn->getIsMoving()) {
            pn->setIsMoving(false);
        }
        // Fire every 0.7 seconds
        if (now - lastShotTime > 0.45)
        {
            if (pn->CanShoot())
            {
                if (ClearAllyLineOfFire(pn, primaryTarget, allies)) {
                    pn->Shoot(primaryTarget);
                    lastShotTime = now;

                    if (!primaryTarget->IsAlive())
                    {
                        printf("[COMBAT] [%c] eliminated %c\n",
                            pn->getSymbol(), primaryTarget->getSymbol());
                    }
                } else {
                    printf("[COMBAT] [%c] holding fire to avoid friendly line-of-fire.\n", pn->getSymbol());
                }
            }
            else if (pn->getRole() == Role::Warrior)
            {
                pn->setLowAmmo(true);
                pn->ReportLowAmmo();
                printf("[COMBAT] [%c] out of ammo, requesting supply.\n", pn->getSymbol());
                OnExit(pn);
                pn->setCurrentState(new GoToSupply());
                pn->getCurrentState()->OnEnter(pn);
                return;
            }
        }
    }
    // If no visible enemy but we have grenade target (behind cover), throw grenade
    else if (grenadeTarget && pn->getRole() == Role::Warrior && pn->CanThrowGrenade() && now - lastShotTime > 1.5)
    {
        double gx = grenadeTarget->getX();
        double gy = grenadeTarget->getY();
        const double allySafetyRadiusSq = 64.0; // radius 8 cells

        if (!AlliesWithinRadius(allies, pn, gx, gy, allySafetyRadiusSq)) {
            lastThrowerRole = pn->getRole();
            pn->ThrowGrenade(gx, gy);
            lastShotTime = now;
            printf("[COMBAT] [%c] launched grenade at hidden enemy.\n", pn->getSymbol());
        }
    }
    else
    {
        if (pn->getIsMoving()) return;
        // No visible enemies – continue advancing toward enemy base
        // Don't automatically go to cover, let commander decide
        // If no path, try to find a new target
        if (!pn->getIsMoving() && pn->getPathSize() == 0) {
            if (nearestEnemy && nearestEnemy->IsAlive()) {
                pn->GoToGrid((int)std::round(nearestEnemy->getX()), (int)std::round(nearestEnemy->getY()));
            } else {
                // fallback to advancing toward enemy base
                int targetX, targetY;
                if (pn->getTeam() == TeamId::Orange) {
                    targetX = 140;  // toward Blue base
                    targetY = 60;
                }
                else {
                    targetX = 60;   // toward Orange base
                    targetY = 40;
                }
                pn->GoToGrid(targetX, targetY);
            }
        }
        else {
            // close but obstructed? try to reposition
            NPC* obstructed = nullptr;
            double bestDist2 = FIRE_RANGE * FIRE_RANGE;
            for (NPC* enemy : enemies) {
                if (!enemy || !enemy->IsAlive()) continue;
                double dx = enemy->getX() - pn->getX();
                double dy = enemy->getY() - pn->getY();
                double dist2 = dx * dx + dy * dy;
                if (dist2 < bestDist2 && dist2 > 9.0) { // not standing on same tile
                    if (!pn->CanSee(enemy)) {
                        obstructed = enemy;
                        bestDist2 = dist2;
                    }
                }
            }
            if (obstructed) {
                int tx = static_cast<int>(std::round(obstructed->getX()));
                int ty = static_cast<int>(std::round(obstructed->getY()));
                int altX = tx;
                int altY = ty;
                if (Map::FindNearestFreeTile(tx, ty, 2, altX, altY, pn)) {
                    pn->GoToGrid(altX, altY);
                } else {
                    pn->GoToGrid(tx, ty);
                }
            }
        }
    }

    // === 3) Retreat if badly injured ===
    if (!recentlyRetreated && pn->getHP() < INJURY_THRESHOLD)
    {
        pn->ReportInjury();
        printf("[COMBAT] [%c] HP=%d, falling back to cover.\n",
            pn->getSymbol(), pn->getHP());
        ExitCombatState(pn);
        pn->setCurrentState(new GoToCover());
        pn->MarkRetreat();
        pn->getCurrentState()->OnEnter(pn);
        return;
    }
}

// When combat ends
void GoToCombat::OnExit(NPC* pn)
{
    ExitCombatState(pn);
}

