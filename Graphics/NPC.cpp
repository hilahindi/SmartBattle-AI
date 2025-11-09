#include "NPC.h"
#include "glut.h"
#include <math.h>
#include <cmath>
#include <stdio.h>
#include <time.h>
#include <queue>
#include "Map.h"
#include "Pathfinding.h"
#include <algorithm>
#include "Roles.h"
#include "GoToCover.h"
#include "GoToCombat.h"
#include "GoToHeal.h"
#include "GoDeliverAmmo.h"
#include "GoToSupply.h"
#include "GoToMedSupply.h"
#include "Commander.h"
#include "Definitions.h"
#include "Grenade.h"
#include <algorithm>
#include <limits>

int NPC::nextId = 1;

extern std::vector<NPC*> teamOrange;
extern std::vector<NPC*> teamBlue;

// Global list of active grenades (defined in NPC.cpp, declared in main.cpp)
std::vector<Grenade*> activeGrenades;

// Gunshot projectile handling
struct Gunshot
{
    double x, y;
    double dirX, dirY;
    double speed;
    double remainingDistance;
    TeamId team;
    int damage;
    NPC* shooter;
};

namespace {
    inline double ClampDouble(double value, double minVal, double maxVal)
    {
        if (value < minVal) return minVal;
        if (value > maxVal) return maxVal;
        return value;
    }
}

static std::vector<Gunshot> activeGunshots;

static void SpawnGunshot(NPC* shooter, NPC* target)
{
    if (!shooter || !target) return;
    if (shooter->getRole() != Role::Warrior) return; // only warriors fire bullets

    double dx = target->getX() - shooter->getX();
    double dy = target->getY() - shooter->getY();
    double dist = sqrt(dx * dx + dy * dy);
    if (dist < 1e-4) return;

    Gunshot shot;
    shot.dirX = dx / dist;
    shot.dirY = dy / dist;
    shot.x = shooter->getX() + shot.dirX * 0.5;
    shot.y = shooter->getY() + shot.dirY * 0.5;
    shot.speed = 0.8;
    shot.remainingDistance = FIRE_RANGE;
    shot.team = shooter->getTeam();
    shot.damage = BULLET_DAMAGE;
    shot.shooter = shooter;
    activeGunshots.push_back(shot);
}



// Decide initial FSM state based on role
void NPC::AssignInitialStateByRole()
{
    if (pCurrentState) delete pCurrentState;

    switch (role)
    {
    case Role::Commander:
        pCurrentState = new GoToCover();
        break;
    case Role::Warrior:
        pCurrentState = new GoToCombat();
        break;
    case Role::Medic:
        pCurrentState = new GoToMedSupply();
        break;
    case Role::Porter:
        pCurrentState = new GoToSupply();
        break;
    default:
        pCurrentState = nullptr;
        break;
    }

    pInterruptedState = nullptr;
    if (pCurrentState)
        pCurrentState->OnEnter(this);
}


// Constructor
NPC::NPC(double posX, double posY, TeamId t, Role r, double sz)
    : x(posX), y(posY),
    targetX(posX), targetY(posY),
    dirX(0), dirY(0),
    isMoving(false), isEngaging(false), isDelivering(false),
    isResting(false), isLowAmmo(false),
    id(nextId++), occupiedCellX(-1), occupiedCellY(-1), hasOccupancy(false),
    waitTicks(0), stuckTicks(0), lastProgressX(posX), lastProgressY(posY),
    pendingFullReplan(false), blockCounter(0),
    team(t), role(r), size(sz)
{
    pCurrentState = nullptr;
    pInterruptedState = nullptr;
    pathIndex = -1;
    ammo = 0;
    grenades = 0;
    maxAmmo = 0;
    maxSupply = 0;
    supply = 0;
    assistsDone = 0;
    targetNPC = nullptr;
    hasOrderTarget = false;
    orderTargetX = posX;
    orderTargetY = posY;
    hp = 100;
    blockedSinceTime = 0.0;
    blockedCellX = -1;
    blockedCellY = -1;
    lastRetreatTime = 0.0;
    lastIdleAnchorTime = 0.0;
    hitFlashUntil = 0.0;
    lastBlockedTime = 0.0;
    blockedAttempts = 0;
    replanAttempts = 0;
    lastReplanAttemptTime = 0.0;
    lastEnemyReportTime = 0.0;
    UpdateOccupancy();

    switch (role)
    {
    case Role::Warrior:
        maxAmmo = MAX_AMMO;
        ammo = MAX_AMMO;
        grenades = MAX_GRENADES;
        maxSupply = MAX_GRENADES;
        supply = grenades;
        break;
    case Role::Medic:
        maxAmmo = 0;
        ammo = 0;
        grenades = 0;
        maxSupply = MEDIC_MAX_SUPPLIES;
        supply = MEDIC_MAX_SUPPLIES;
        break;
    case Role::Porter:
        maxAmmo = 0;
        ammo = 0;
        grenades = 0;
        maxSupply = PORTER_MAX_SUPPLIES;
        supply = PORTER_MAX_SUPPLIES;
        break;
    case Role::Commander:
    default:
        maxAmmo = 0;
        ammo = 0;
        grenades = 0;
        maxSupply = 0;
        supply = 0;
        break;
    }

    if (ammo <= LOW_AMMO_THRESHOLD) {
        isLowAmmo = true;
    }
    //AssignInitialStateByRole();
}

NPC::~NPC()
{
    if (hasOccupancy) {
        Map::ClearOccupied(occupiedCellX, occupiedCellY, id);
        hasOccupancy = false;
    }
    State* current = pCurrentState;
    State* interrupted = pInterruptedState;
    if (current) {
        delete current;
    }
    if (interrupted && interrupted != current) {
        delete interrupted;
    }
    pCurrentState = nullptr;
    pInterruptedState = nullptr;
}

// Draw as colored square with centered role letter
void NPC::DrawAsSquareWithLetter() const
{
    TeamColor c = GetTeamColor(team);
    glColor3d(c.r, c.g, c.b);
    double h = size * 0.5;

    // Fill
    glBegin(GL_POLYGON);
    glVertex2d(x - h, y - h);
    glVertex2d(x + h, y - h);
    glVertex2d(x + h, y + h);
    glVertex2d(x - h, y + h);
    glEnd();

    // Outline
    glColor3d(0, 0, 0);
    glBegin(GL_LINE_LOOP);
    glVertex2d(x - h, y - h);
    glVertex2d(x + h, y - h);
    glVertex2d(x + h, y + h);
    glVertex2d(x - h, y + h);
    glEnd();

    // Role symbol
    void* font = GLUT_BITMAP_HELVETICA_12;
    double px = x - 0.5;
    double py = y - 0.5;
    glRasterPos2d(px, py);
    glutBitmapCharacter(font, getSymbol());
}

// Assign existing path for following
void NPC::SetPath(const std::vector<std::pair<int, int>>& p)
{
    path = p;
    if (!path.empty()) {
        pathIndex = 0;
        targetX = (double)path[pathIndex].first;
        targetY = (double)path[pathIndex].second;
        setDirection();
        isMoving = true;
    }
    else {
        pathIndex = -1;
        isMoving = false;
    }
    ClearPathBlocking();
    pendingFullReplan = false;
}

bool NPC::isBusy() const
{
    return isMoving || isEngaging || isDelivering;
}

// Compute A* path and start moving
// Now uses FindSafePath (A* with security map)
void NPC::GoToGrid(int gx, int gy)
{
    int sx = (int)(x + 0.5);
    int sy = (int)(y + 0.5);
    if (sx == gx && sy == gy) {
        printf("[INFO] [%c] already at (%d,%d), skipping path request.\n",
            getSymbol(), gx, gy);
        path.clear();
        pathIndex = -1;
        isMoving = false;
        return;
    }
    std::vector<std::pair<int, int>> p;

    printf(" [%c] Safe path request: (%d,%d) -> (%d,%d)\n", getSymbol(), sx, sy, gx, gy);

    if (Path::FindSafePath(sx, sy, gx, gy, team, p, 0.8, id)) {
        printf(" [%c] SAFE path found! length = %zu\n", getSymbol(), p.size());
        SetPath(p);
    }
    else {
        printf("[WARN] [%c] no safe path found. Trying regular path.\n", getSymbol());
        if (Path::FindPath(sx, sy, gx, gy, p, id)) {
            SetPath(p);
        }
        else {
            isMoving = false;
                    path.clear();
                    pathIndex = -1;
        }
    }
}

bool NPC::TryPlanAroundOccupiedCell(int blockedX, int blockedY, int goalX, int goalY)
{
    if (goalX == -1 || goalY == -1) return false;
    Map::Cell original = Map::Get(blockedX, blockedY);
    bool modifiedCell = false;
    if (Map::IsWalkable(blockedX, blockedY)) {
        Map::Set(blockedX, blockedY, Map::ROCK);
        modifiedCell = true;
    }

    std::vector<std::pair<int, int>> newPath;
    int sx = (int)(x + 0.5);
    int sy = (int)(y + 0.5);
    bool success =
        Path::FindSafePath(sx, sy, goalX, goalY, team, newPath, 0.8, id) ||
        Path::FindSafePath(sx, sy, goalX, goalY, team, newPath, 0.5, id) ||
        Path::FindPath(sx, sy, goalX, goalY, newPath, id);

    if (modifiedCell) {
        Map::Set(blockedX, blockedY, original);
    }

    if (success && !newPath.empty()) {
        SetPath(newPath);
        return true;
    }
    return false;
}

bool NPC::PlanShortDetour(int searchRadius)
{
    int startX = static_cast<int>(std::round(x));
    int startY = static_cast<int>(std::round(y));
    if (!Map::InBounds(startX, startY))
        return false;

    const int width = Map::W;
    const int height = Map::H;
    auto indexOf = [width](int px, int py) { return py * width + px; };

    std::vector<int> parent(width * height, -1);
    std::vector<char> visited(width * height, 0);
    std::queue<std::pair<int, int>> q;
    q.push({ startX, startY });
    parent[indexOf(startX, startY)] = indexOf(startX, startY);
    visited[indexOf(startX, startY)] = 1;

    auto withinRadius = [&](int px, int py) {
        return std::max(std::abs(px - startX), std::abs(py - startY)) <= searchRadius;
        };

    std::pair<int, int> escape = { -1, -1 };
    const int OFFSETS[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };

    while (!q.empty()) {
        auto current = q.front();
        q.pop();

        for (auto& offset : OFFSETS) {
            int nx = current.first + offset[0];
            int ny = current.second + offset[1];
            if (!Map::InBounds(nx, ny)) continue;
            if (!withinRadius(nx, ny)) continue;
            int idx = indexOf(nx, ny);
            if (visited[idx]) continue;
        if (!Map::IsWalkable(nx, ny)) continue;
        if (Map::IsOccupied(nx, ny, id)) continue;

            visited[idx] = 1;
            parent[idx] = indexOf(current.first, current.second);
            escape = { nx, ny };
            break;
        }
        if (escape.first != -1)
            break;

        for (auto& offset : OFFSETS) {
            int nx = current.first + offset[0];
            int ny = current.second + offset[1];
            if (!Map::InBounds(nx, ny)) continue;
            if (!withinRadius(nx, ny)) continue;
            int idx = indexOf(nx, ny);
            if (visited[idx]) continue;
            if (!Map::IsWalkable(nx, ny)) continue;
            if (Map::IsOccupied(nx, ny, id)) continue;

            visited[idx] = 1;
            parent[idx] = indexOf(current.first, current.second);
            q.push({ nx, ny });
        }
    }

    if (escape.first == -1)
        return false;

    std::vector<std::pair<int, int>> detour;
    int cursor = indexOf(escape.first, escape.second);
    const int startIdx = indexOf(startX, startY);
    while (cursor != startIdx) {
        int px = cursor % width;
        int py = cursor / width;
        detour.push_back({ px, py });
        cursor = parent[cursor];
        if (cursor < 0) break;
    }
    if (detour.empty())
        return false;

    NPC* commanderNPC = nullptr;
    const std::vector<NPC*>& allies = (team == TeamId::Orange) ? teamOrange : teamBlue;
    for (NPC* ally : allies) {
        if (ally && ally->IsAlive() && ally->getRole() == Role::Commander) {
            commanderNPC = ally;
            break;
        }
    }

    if (commanderNPC) {
        double cx = commanderNPC->getX();
        double cy = commanderNPC->getY();
        auto farEnough = [&](const std::pair<int, int>& cell) {
            double dx = cx - cell.first;
            double dy = cy - cell.second;
            return (dx * dx + dy * dy) > 9.0;
        };
        if (!farEnough(detour.front())) {
            for (auto it = detour.begin(); it != detour.end(); ++it) {
                if (farEnough(*it)) {
                    detour.erase(detour.begin(), it);
                    break;
                }
            }
        }
        if (detour.empty())
            return false;
    }

    std::reverse(detour.begin(), detour.end());

    std::vector<std::pair<int, int>> remainder;
    if (pathIndex >= 0 && pathIndex < (int)path.size()) {
        remainder.assign(path.begin() + pathIndex, path.end());
    }

    if (!remainder.empty() && detour.back() == remainder.front()) {
        remainder.erase(remainder.begin());
    }

    detour.insert(detour.end(), remainder.begin(), remainder.end());

    printf("[INFO] [%c] planning short detour via (%d,%d)\n",
        getSymbol(), escape.first, escape.second);
    SetPath(detour);
    return true;
}

bool NPC::TryStepAside()
{
    if (pathIndex < 0 || pathIndex >= (int)path.size())
        return false;

    int baseX = static_cast<int>(std::round(x));
    int baseY = static_cast<int>(std::round(y));
    if (!Map::InBounds(baseX, baseY))
        return false;

    int nextX = path[pathIndex].first;
    int nextY = path[pathIndex].second;
    int dirX = nextX - baseX;
    int dirY = nextY - baseY;

    if (dirX == 0 && dirY == 0)
        return false;

    const std::pair<int, int> goal = path.empty() ? std::make_pair(nextX, nextY) : path.back();

    std::vector<std::pair<int, int>> candidates;
    // Orthogonal side steps (perpendicular to movement direction)
    candidates.emplace_back(-dirY, dirX);
    candidates.emplace_back(dirY, -dirX);
    // Slight diagonal drift options
    candidates.emplace_back(dirX - dirY, dirY + dirX);
    candidates.emplace_back(dirX + dirY, dirY - dirX);

    double bestScore = std::numeric_limits<double>::infinity();
    int bestX = baseX;
    int bestY = baseY;
    bool found = false;

    for (const auto& offset : candidates) {
        int nx = baseX + offset.first;
        int ny = baseY + offset.second;
        if (!Map::InBounds(nx, ny)) continue;
        if (!Map::IsWalkable(nx, ny)) continue;
        if (Map::IsOccupied(nx, ny, id)) continue;

        double security = Map::GetSecurityValue(ny, nx, team);
        double occupancyPenalty = Map::GetOccupancyPenalty(nx, ny, id);
        double distGoal = std::abs(goal.first - nx) + std::abs(goal.second - ny);
        double score = security * 10.0 + occupancyPenalty + distGoal * 0.2;

        if (score < bestScore) {
            bestScore = score;
            bestX = nx;
            bestY = ny;
            found = true;
        }
    }

    if (!found)
        return false;

    std::vector<std::pair<int, int>> newPath;
    newPath.emplace_back(bestX, bestY);
    if (pathIndex >= 0 && pathIndex < (int)path.size()) {
        newPath.insert(newPath.end(), path.begin() + pathIndex, path.end());
    }

    printf("[INFO] [%c] stepping aside to (%d,%d)\n", getSymbol(), bestX, bestY);
    SetPath(newPath);
    return true;
}

void NPC::ClearPathBlocking()
{
    blockedCellX = -1;
    blockedCellY = -1;
    blockedSinceTime = 0.0;
}

void NPC::UpdateOccupancy()
{
    int cellX = static_cast<int>(std::round(x));
    int cellY = static_cast<int>(std::round(y));

    if (!Map::InBounds(cellX, cellY)) {
        if (hasOccupancy) {
            Map::ClearOccupied(occupiedCellX, occupiedCellY, id);
            hasOccupancy = false;
        }
        return;
    }

    if (!hasOccupancy || cellX != occupiedCellX || cellY != occupiedCellY) {
        if (hasOccupancy) {
            Map::ClearOccupied(occupiedCellX, occupiedCellY, id);
        }
        Map::SetOccupied(cellX, cellY, id);
        occupiedCellX = cellX;
        occupiedCellY = cellY;
        hasOccupancy = true;
    }
    else {
        Map::SetOccupied(cellX, cellY, id);
    }
}

bool NPC::ReplanPathWithDynamicCosts()
{
    int startCellX = static_cast<int>(std::round(x));
    int startCellY = static_cast<int>(std::round(y));

    int goalX = -1;
    int goalY = -1;

    if (!path.empty()) {
        goalX = path.back().first;
        goalY = path.back().second;
    }
    else if (getHasOrderTarget()) {
        auto order = getOrderTarget();
        goalX = order.first;
        goalY = order.second;
    }

    if (goalX == -1 || goalY == -1)
        return false;

    std::vector<std::pair<int, int>> newPath;
    bool replanned =
        Path::FindSafePath(startCellX, startCellY, goalX, goalY, team, newPath, 0.8, id) ||
        Path::FindSafePath(startCellX, startCellY, goalX, goalY, team, newPath, 0.5, id) ||
        Path::FindPath(startCellX, startCellY, goalX, goalY, newPath, id);

    if (replanned && !newPath.empty()) {
        SetPath(newPath);
        return true;
    }

    return false;
}

// Update direction based on next waypoint
void NPC::setDirection()
{
    double dx = targetX - x, dy = targetY - y;
    double L = sqrt(dx * dx + dy * dy);
    if (L > 1e-6) { dirX = dx / L; dirY = dy / L; }
    else { dirX = dirY = 0; }
}

// Handle movement along current path
void NPC::DoSomeWork()
{
    // Dead NPCs don't do anything
    if (!IsAlive()) {
        isMoving = false;
        return;
    }
    
    // If not moving but we have a valid path, start moving to the next cell
    if (!isMoving && pathIndex >= 0 && pathIndex < (int)path.size()) {
        targetX = (double)path[pathIndex].first;
        targetY = (double)path[pathIndex].second;
        setDirection();
        isMoving = true;
    }

    // If currently moving, calculate next step
    if (isMoving) {
        double nx = x + dirX * SPEED;
        double ny = y + dirY * SPEED;

        int cx = (int)(nx + 0.5);
        int cy = (int)(ny + 0.5);

        bool walkable = Map::IsWalkable(cx, cy);
        bool occupied = Map::IsOccupiedByNPC(cx, cy, teamBlue, teamOrange, this);

        if (walkable && !occupied)
        {
            nx = ClampDouble(nx, 1.0, static_cast<double>(Map::W - 2));
            ny = ClampDouble(ny, 1.0, static_cast<double>(Map::H - 2));
            x = nx;
            y = ny;
            UpdateOccupancy();
            setBlockCounter(0); 
        }
        else {
            int currentCellX = (int)(x + 0.5);
            int currentCellY = (int)(y + 0.5);

            if (occupied) {
                auto findNpcAtCell = [&](int cellX, int cellY) -> NPC*
                {
                    const std::vector<std::vector<NPC*>*> teams = { &teamOrange, &teamBlue };
                    for (const auto* teamVec : teams) {
                        for (NPC* npc : *teamVec) {
                            if (!npc || npc == this || !npc->IsAlive()) continue;
                            int npcCellX = (int)(npc->getX() + 0.5);
                            int npcCellY = (int)(npc->getY() + 0.5);
                            if (npcCellX == cellX && npcCellY == cellY) {
                                return npc;
                            }
                        }
                    }
                    return nullptr;
                };

                NPC* occupant = findNpcAtCell(cx, cy);

                if (occupant && occupant->getIsMoving()) {
                    int occupantTargetX = (int)(occupant->getTargetX() + 0.5);
                    int occupantTargetY = (int)(occupant->getTargetY() + 0.5);
                    if (occupantTargetX == currentCellX && occupantTargetY == currentCellY) {
                        printf(" [%c] yielding swap with %c at (%d,%d)\n",
                            getSymbol(), occupant->getSymbol(), cx, cy);
                        isMoving = false;
                        setBlockCounter(0);
                        return;
                    }
                }

                if (TryStepAside()) {
                    printf(" [%c] sidestepped to avoid block at (%d,%d)\n",
                        getSymbol(), cx, cy);
                    setBlockCounter(0);
                    return;
                }
            }

            if (!walkable) {
                printf(" [%c] terrain blockage at (%d,%d)\n", getSymbol(), cx, cy);
            }

            printf(" [%c] blocked at (%d,%d). Counter: %d\n", getSymbol(), cx, cy, getBlockCounter());
            isMoving = false; 
            
            setBlockCounter(getBlockCounter() + 1);
            
            if (getBlockCounter() > 5) {
                printf(" [%c] Persistent block detected. Clearing path and retreating to cover.\n", getSymbol());
                
                path.clear(); 
                pathIndex = -1;
                setBlockCounter(0); 
                
                if (getCurrentState()) delete getCurrentState();
                setCurrentState(new GoToCover());
                getCurrentState()->OnEnter(this);
                return; 
            }
            return; 
        }

        // Check if target reached
        double dx = targetX - x, dy = targetY - y;
        if (dx * dx + dy * dy < 0.25) {
            x = targetX;
            y = targetY;
            UpdateOccupancy();
            pathIndex++;

            if (pathIndex < (int)path.size()) {
                targetX = (double)path[pathIndex].first;
                targetY = (double)path[pathIndex].second;
                setDirection();
                isMoving = true;
            }
            else {
                printf("🟢 [%c] reached destination (%.1f, %.1f)\n", getSymbol(), x, y);
                isMoving = false;
                path.clear();
                pathIndex = -1;
            }
        }
    }
}

void NPC::MarkRetreat()
{
    lastRetreatTime = clock() / (double)CLOCKS_PER_SEC;
}

void NPC::MarkIdleAnchorIssued()
{
    lastIdleAnchorTime = clock() / (double)CLOCKS_PER_SEC;
}

// Combat and status reports
void NPC::ReportEnemySpotted(int ex, int ey) {
    double now = clock() / (double)CLOCKS_PER_SEC;
    if (lastEnemyReportTime > 0.0 && (now - lastEnemyReportTime) < ENEMY_REPORT_COOLDOWN) {
        return;
    }
    lastEnemyReportTime = now;

    if (commander) {
        commander->ReceiveReport(this, ReportType::ENEMY_SPOTTED);
    }
}

void NPC::ReportLowAmmo() {
    if (ammo <= LOW_AMMO_THRESHOLD && !isLowAmmo) {
        isLowAmmo = true;
        printf(" [%c] reporting low ammo! (ammo=%d)\n", getSymbol(), ammo);
        if (commander) commander->ReceiveReport(this, ReportType::LOW_AMMO);
    }
}


void NPC::ReportInjury() {
    if (hp < INJURY_THRESHOLD && hp > 0) {
        printf(" [%c] injured (hp=%d)\n", getSymbol(), hp);
        if (commander) commander->ReceiveReport(this, ReportType::INJURED);
    }
}


void NPC::TakeDamage(int dmg) {
    double now = clock() / (double)CLOCKS_PER_SEC;
    hp -= dmg;
    if (hp <= 0) {
        hp = 0;
        isMoving = false;
        path.clear();
        pathIndex = -1;
        printf(" [%c] eliminated!\n", getSymbol());
    }
    else {
        ReportInjury();
        hitFlashUntil = std::max(hitFlashUntil, now + 0.4);
        if (hp < 40) {
            State* current = getCurrentState();
            GoToCover* coverState = dynamic_cast<GoToCover*>(current);
            if (!coverState) {
                setCurrentState(new GoToCover());
                MarkRetreat();
                hitFlashUntil = std::max(hitFlashUntil, now + 0.6);
                if (getCurrentState()) {
                    getCurrentState()->OnEnter(this);
                }
            }
        }
    }
}

void NPC::HealSelf(int amount) {
    hp = std::min(100, hp + amount);
    printf(" [%c] healed to %d HP\n", getSymbol(), hp);
}

// Combat helpers
bool NPC::InRange(NPC* target, double range) const {
    double dx = target->getX() - x;
    double dy = target->getY() - y;
    return (dx * dx + dy * dy) <= (range * range);
}

bool NPC::CanSee(NPC* target) const {
    return Map::IsLineOfSightClear((int)x, (int)y, (int)target->getX(), (int)target->getY());
}

void NPC::Shoot(NPC* target) {
    if (!CanShoot()) return;   // Only warriors with ammo can shoot
    if (!target || !target->IsAlive()) return;
    if (!InRange(target, FIRE_RANGE)) return;
    if (!CanSee(target)) return;

    decreaseAmmo();
    SpawnGunshot(this, target);
    printf(" [%c] fired at %c! Ammo left: %d\n", getSymbol(), target->getSymbol(), ammo);
    
    // Check if ammo is low
    if (ammo <= LOW_AMMO_THRESHOLD) {
        setLowAmmo(true);
        ReportLowAmmo();
    }
}

void NPC::ThrowGrenade(double targetX, double targetY) {
    if (role != Role::Warrior) {
        printf("[WARN] [%c] attempt to throw grenade blocked (role %d).\n", getSymbol(), (int)role);
        return;
    }
    if (!CanThrowGrenade()) {
        printf("[WARN] [%c] cannot throw grenades (insufficient grenades).\n", getSymbol());
        return;
    }
    
    // Check if in range
    double dx = targetX - x;
    double dy = targetY - y;
    double dist2 = dx * dx + dy * dy;
    if (dist2 > GRENADE_RANGE * GRENADE_RANGE) return;
    
    printf("[INFO] %c invoking ThrowGrenade (role=%d, grenades=%d) target=(%.1f, %.1f)\n",
        getSymbol(), (int)role, grenades, targetX, targetY);
    decreaseGrenades();
    if (role == Role::Warrior) {
        supply = grenades;
    }
    
    // Create grenade at target position
    Grenade* grenade = new Grenade(targetX, targetY);
    grenade->SetIsExploding(true);
    activeGrenades.push_back(grenade);
    
    printf("[GRENADE] [%c] threw grenade at (%.1f, %.1f)!\n", getSymbol(), targetX, targetY);
    
    // Damage enemies hit by grenade bullets
    extern std::vector<NPC*> teamOrange;
    extern std::vector<NPC*> teamBlue;
    const std::vector<NPC*>& enemies = (team == TeamId::Orange) ? teamBlue : teamOrange;
    
    for (NPC* enemy : enemies) {
        if (!enemy || !enemy->IsAlive()) continue;
        double ex = enemy->getX();
        double ey = enemy->getY();
        double dist2 = (ex - targetX) * (ex - targetX) + (ey - targetY) * (ey - targetY);
        const double maxRadius = 36.0; // 6 cells radius squared
        if (dist2 <= maxRadius) {
            double dist = sqrt(dist2);
            double factor = std::max(0.0, 1.0 - (dist / 6.0));
            int damage = std::max(1, (int)std::round(GRENADE_DAMAGE * factor));
            enemy->TakeDamage(damage);
            printf("[EXPLOSION] Grenade hit %c! Damage=%d HP: %d\n", enemy->getSymbol(), damage, enemy->getHP());
        }
    }
}

void NPC::Reload(int amount) {
    if (maxAmmo <= 0) return;
    ammo += amount;
    if (ammo > maxAmmo) ammo = maxAmmo;
    printf(" [%c] reloaded -> ammo = %d\n", getSymbol(), ammo);
    if (ammo > LOW_AMMO_THRESHOLD) {
        setLowAmmo(false);
    }
}

void NPC::RefillAmmo() {
    if (maxAmmo <= 0) return;
    ammo = maxAmmo;
    setLowAmmo(false);
    printf(" [%c] ammo refilled -> ammo = %d\n", getSymbol(), ammo);
    if (role == Role::Warrior) {
        grenades = MAX_GRENADES;
        supply = maxSupply;
        printf(" [%c] grenades restocked -> grenades = %d\n", getSymbol(), grenades);
    }
}

bool NPC::NeedsAmmo() const {
    if (role != Role::Warrior) return false;
    return ammo <= LOW_AMMO_THRESHOLD;
}

void NPC::RegisterAssistCompletion() {
    if (role == Role::Medic || role == Role::Porter) {
        assistsDone = std::min(assistsDone + 1, ASSIST_LIMIT);
    }
}

// Draw HP bar above the NPC
void NPC::DrawHPBar(bool placeAbove) const
{
    double barWidth = size * 1.5;
    double barHeight = 0.9;
    double barX = x - barWidth / 2.0;
    double offset = placeAbove ? -(size + 1.8) : (size + 1.8);
    double barY = y + offset;
    
    // Background grey
    glColor3d(0.2, 0.2, 0.2);
    glBegin(GL_POLYGON);
    glVertex2d(barX, barY);
    glVertex2d(barX + barWidth, barY);
    glVertex2d(barX + barWidth, barY + barHeight);
    glVertex2d(barX, barY + barHeight);
    glEnd();
    
    // HP fill (light grey)
    double hpPercent = (double)hp / 100.0;
    double fillWidth = barWidth * hpPercent;
    
    glColor3d(0.75, 0.75, 0.75);
    
    if (fillWidth > 0) {
        glBegin(GL_POLYGON);
        glVertex2d(barX, barY);
        glVertex2d(barX + fillWidth, barY);
        glVertex2d(barX + fillWidth, barY + barHeight);
        glVertex2d(barX, barY + barHeight);
        glEnd();
    }
    
    // Border
    glColor3d(0.0, 0.0, 0.0);
    glLineWidth(1.0);
    glBegin(GL_LINE_LOOP);
    glVertex2d(barX, barY);
    glVertex2d(barX + barWidth, barY);
    glVertex2d(barX + barWidth, barY + barHeight);
    glVertex2d(barX, barY + barHeight);
    glEnd();
    
    // HP text with percentage (to the right of the bar)
    void* font = GLUT_BITMAP_HELVETICA_10;
    
    char hpStr[16];
    sprintf_s(hpStr, sizeof(hpStr), "HP:%d%%", hp);
    
    // Calculate text position (centered)
    double textX = barX + barWidth + 1.0;
    double textY = barY + barHeight / 2.0 - 0.12;
    
    // White text for better visibility
    glColor3d(1.0, 1.0, 1.0);
    glRasterPos2d(textX, textY);
    for (const char* c = hpStr; *c != '\0'; c++) {
        glutBitmapCharacter(font, *c);
    }
}

void NPC::DrawAmmoBar(bool placeAbove) const
{
    if (maxAmmo <= 0) return;

    double percent = 0.0;
    if (maxAmmo > 0) {
        double ratio = ammo / (double)maxAmmo;
        percent = std::max(0.0, std::min(1.0, ratio));
    }
    double barWidth = size * 1.5;
    double barHeight = 0.65;
    double barX = x - barWidth / 2.0;
    double offset = placeAbove ? -(size + 3.0) : (size + 3.0);
    double barY = y + offset;

    glColor3d(0.2, 0.2, 0.2); // grey background
    glBegin(GL_POLYGON);
    glVertex2d(barX, barY);
    glVertex2d(barX + barWidth, barY);
    glVertex2d(barX + barWidth, barY + barHeight);
    glVertex2d(barX, barY + barHeight);
    glEnd();

    glColor3d(0.75, 0.75, 0.75); // light grey fill
    if (percent > 0) {
        glBegin(GL_POLYGON);
        glVertex2d(barX, barY);
        glVertex2d(barX + barWidth * percent, barY);
        glVertex2d(barX + barWidth * percent, barY + barHeight);
        glVertex2d(barX, barY + barHeight);
        glEnd();
    }

    glColor3d(0.0, 0.0, 0.0);
    glBegin(GL_LINE_LOOP);
    glVertex2d(barX, barY);
    glVertex2d(barX + barWidth, barY);
    glVertex2d(barX + barWidth, barY + barHeight);
    glVertex2d(barX, barY + barHeight);
    glEnd();

    // Ammo label and values
    void* font = GLUT_BITMAP_HELVETICA_10;
    char ammoStr[32];
    sprintf_s(ammoStr, sizeof(ammoStr), "A:%d/%d", ammo, maxAmmo);
    glColor3d(1.0, 1.0, 1.0);
    double textX = barX + barWidth + 1.0;
    double textY = barY + barHeight / 2.0 - 0.1;
    glRasterPos2d(textX, textY);
    for (const char* c = ammoStr; *c != '\0'; ++c) {
        glutBitmapCharacter(font, *c);
    }
}

void NPC::DrawSupplyBar(bool placeAbove) const
{
    int current = 0;
    int maxValue = 0;

    if (role == Role::Warrior) {
        current = grenades;
        maxValue = MAX_GRENADES;
    }
    else {
        maxValue = maxSupply;
        current = supply;
    }

    if (maxValue <= 0) return;

    double ratio = (maxValue > 0) ? current / (double)maxValue : 0.0;
    double percent = std::max(0.0, std::min(1.0, ratio));
    double barWidth = size * 1.5;
    double barHeight = 0.65;
    double barX = x - barWidth / 2.0;
    double offset = placeAbove ? -(size + 4.2) : (size + 4.2);
    double barY = y + offset;

    glColor3d(0.2, 0.2, 0.2); // grey background
    glBegin(GL_POLYGON);
    glVertex2d(barX, barY);
    glVertex2d(barX + barWidth, barY);
    glVertex2d(barX + barWidth, barY + barHeight);
    glVertex2d(barX, barY + barHeight);
    glEnd();

    glColor3d(0.75, 0.75, 0.75); // light grey fill
    if (percent > 0) {
        glBegin(GL_POLYGON);
        glVertex2d(barX, barY);
        glVertex2d(barX + barWidth * percent, barY);
        glVertex2d(barX + barWidth * percent, barY + barHeight);
        glVertex2d(barX, barY + barHeight);
        glEnd();
    }

    glColor3d(0.0, 0.0, 0.0);
    glBegin(GL_LINE_LOOP);
    glVertex2d(barX, barY);
    glVertex2d(barX + barWidth, barY);
    glVertex2d(barX + barWidth, barY + barHeight);
    glVertex2d(barX, barY + barHeight);
    glEnd();

    // Supply / grenade label and values
    const char* label = "Supply";
    if (role == Role::Warrior) label = "Grenades";
    else if (role == Role::Medic) label = "Medkit";
    else if (role == Role::Porter) label = "Crate";

    void* font = GLUT_BITMAP_HELVETICA_10;
    char supplyStr[40];
    if ((role == Role::Porter || role == Role::Medic) && maxValue <= 1) {
        const char* status = (current > 0) ? "Ready" : "Empty";
        sprintf_s(supplyStr, sizeof(supplyStr), "%c:%s", label[0], status);
    } else {
        sprintf_s(supplyStr, sizeof(supplyStr), "%c:%d/%d", label[0], current, maxValue);
    }
    glColor3d(1.0, 1.0, 1.0);
    double textX = barX + barWidth + 1.0;
    double textY = barY + barHeight / 2.0 - 0.1;
    glRasterPos2d(textX, textY);
    for (const char* c = supplyStr; *c != '\0'; ++c) {
        glutBitmapCharacter(font, *c);
    }
}

void NPC::DrawDeadMarker() const
{
    double h = size * 0.5;
    glColor3d(0.3, 0.3, 0.3);
    glBegin(GL_POLYGON);
    glVertex2d(x - h, y - h);
    glVertex2d(x + h, y - h);
    glVertex2d(x + h, y + h);
    glVertex2d(x - h, y + h);
    glEnd();

    glColor3d(0.0, 0.0, 0.0);
    glBegin(GL_LINES);
    glVertex2d(x - h, y - h);
    glVertex2d(x + h, y + h);
    glVertex2d(x - h, y + h);
    glVertex2d(x + h, y - h);
    glEnd();
}

void NPC::DrawStatusBars() const
{
    bool placeAbove = y > 12.0;
    DrawHPBar(placeAbove);

    switch (role) {
    case Role::Warrior:
        DrawAmmoBar(placeAbove);
        DrawSupplyBar(placeAbove);
        break;
    case Role::Porter:
    case Role::Medic:
        DrawSupplyBar(placeAbove);
        break;
    default:
        break;
    }
}

double NPC::getMoveSpeed() const
{
    double base = SPEED;
    switch (role) {
    case Role::Medic:
        base *= 1.6;
        break;
    case Role::Porter:
        base *= 1.3;
        break;
    default:
        break;
    }
    return base;
}

void UpdateActiveGunshots()
{
    for (size_t i = 0; i < activeGunshots.size(); )
    {
        Gunshot& shot = activeGunshots[i];

        shot.x += shot.dirX * shot.speed;
        shot.y += shot.dirY * shot.speed;
        shot.remainingDistance -= shot.speed;

        bool removeShot = false;

        int cx = (int)(shot.x + 0.5);
        int cy = (int)(shot.y + 0.5);

        if (!Map::InBounds(cx, cy)) {
            removeShot = true;
        }
        else {
            Map::Cell cell = Map::Get(cx, cy);
            if (cell == Map::ROCK || cell == Map::WAREHOUSE || cell == Map::TREE) {
                removeShot = true;
            }
        }

        if (!removeShot) {
            Map::AddFireRiskAt(cx, cy, shot.team, 0.002);

            std::vector<NPC*>& enemies = (shot.team == TeamId::Orange) ? teamBlue : teamOrange;
            for (NPC* enemy : enemies) {
                if (!enemy || !enemy->IsAlive()) continue;
                double dx = enemy->getX() - shot.x;
                double dy = enemy->getY() - shot.y;
                if (dx * dx + dy * dy <= 1.0) {
                    enemy->TakeDamage(shot.damage);
                    removeShot = true;
                    break;
                }
            }
        }

        if (shot.remainingDistance <= 0.0) {
            removeShot = true;
        }

        if (removeShot) {
            activeGunshots.erase(activeGunshots.begin() + i);
        }
        else {
            ++i;
        }
    }
}

void DrawActiveGunshots()
{
    glLineWidth(3.0);
    for (const Gunshot& shot : activeGunshots)
    {
        TeamColor c = GetTeamColor(shot.team);
        glColor3d(c.r, c.g, c.b);

        double tipX = shot.x + shot.dirX * 1.2;
        double tipY = shot.y + shot.dirY * 1.2;
        double tailX = shot.x - shot.dirX * 0.6;
        double tailY = shot.y - shot.dirY * 0.6;
        double orthoX = -shot.dirY * 0.3;
        double orthoY = shot.dirX * 0.3;

        glBegin(GL_TRIANGLES);
        glVertex2d(tailX + orthoX, tailY + orthoY);
        glVertex2d(tailX - orthoX, tailY - orthoY);
        glVertex2d(tipX, tipY);
        glEnd();

        glColor3d(1.0, 1.0, 0.0);
        glBegin(GL_LINES);
        glVertex2d(tailX, tailY);
        glVertex2d(tipX, tipY);
        glEnd();

        glColor3d(1.0, 0.6, 0.1);
        glBegin(GL_LINES);
        glVertex2d(tailX + orthoX * 0.15, tailY + orthoY * 0.15);
        glVertex2d(tipX, tipY);
        glEnd();
    }
    glLineWidth(1.0);
}
void NPC::Show()
{
    if (IsAlive()) {
        DrawAsSquareWithLetter();
        double now = clock() / (double)CLOCKS_PER_SEC;
        if (now < hitFlashUntil) {
            double outlineHalf = size * 0.65;
            glColor3d(1.0, 0.2, 0.2);
            glLineWidth(3.0);
            glBegin(GL_LINE_LOOP);
            glVertex2d(x - outlineHalf, y - outlineHalf);
            glVertex2d(x + outlineHalf, y - outlineHalf);
            glVertex2d(x + outlineHalf, y + outlineHalf);
            glVertex2d(x - outlineHalf, y + outlineHalf);
            glEnd();
            glLineWidth(1.0);
        }
    }
    else {
        DrawDeadMarker();
    }
    DrawStatusBars();
}
