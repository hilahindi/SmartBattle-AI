#include "Map.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <queue>
#include "glut.h"
#include "NPC.h"
#include "Definitions.h"

extern std::vector<NPC*> teamOrange;
extern std::vector<NPC*> teamBlue;

namespace Map {

    // Internal storage
    static std::vector<Cell> grid;                  // terrain grid
    static std::vector<int> occupancy;              // dynamic occupancy per cell (NPC id)
    static double securityMaps[2][H][W] = { 0.0 };  // danger heatmap per team (0=Orange,1=Blue)
    static double visibilityMap[H][W] = { 0.0 };    // visibility (optional)
    static double dynamicCost[H][W] = { 0.0 };      // temporary inflated costs

    static inline int idx(int x, int y) { return y * W + x; }

    // Basic operations
    void Init() {
        grid.assign(W * H, FREE);
        occupancy.assign(W * H, 0);
        // reset maps too
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                securityMaps[0][y][x] = 0.0;
                securityMaps[1][y][x] = 0.0;
                visibilityMap[y][x] = 0.0;
                dynamicCost[y][x] = 0.0;
            }
        }
    }

    static inline size_t TeamIndex(TeamId team)
    {
        return (team == TeamId::Blue) ? 1u : 0u;
    }

    bool InBounds(int x, int y) {
        return (x >= 0 && x < W && y >= 0 && y < H);
    }

    Cell Get(int x, int y) {
        if (!InBounds(x, y)) return ROCK; // treat out-of-bounds as solid
        return grid[idx(x, y)];
    }

    void Set(int x, int y, Cell c) {
        if (InBounds(x, y)) grid[idx(x, y)] = c;
    }

    // Characters can walk through FREE or TREE (per your spec),
    // cannot walk through ROCK, WATER, WAREHOUSE.
    bool IsWalkable(int x, int y) {
        Cell c = Get(x, y);
        return (c == FREE || c == TREE);
    }

    bool IsOccupied(int x, int y, int ignoreNpcId) {
        if (!InBounds(x, y)) return false;
        int occ = occupancy[idx(x, y)];
        if (occ == 0) return false;
        if (ignoreNpcId >= 0 && occ == ignoreNpcId) return false;
        return true;
    }

    void SetOccupied(int x, int y, int byNpcId) {
        if (!InBounds(x, y) || byNpcId <= 0) return;
        int& occ = occupancy[idx(x, y)];
        if (occ == byNpcId) return;
        if (occ == 0) {
            occ = byNpcId;
        }
    }

    void ClearOccupied(int x, int y, int byNpcId) {
        if (!InBounds(x, y) || byNpcId <= 0) return;
        int& occ = occupancy[idx(x, y)];
        if (occ == byNpcId) {
            occ = 0;
        }
    }

    double GetOccupancyPenalty(int x, int y, int ignoreNpcId) {
        return IsOccupied(x, y, ignoreNpcId) ? 5.0 : 0.0;
    }

    double GetDynamicCost(int x, int y) {
        if (!InBounds(x, y)) return 0.0;
        return dynamicCost[y][x];
    }

    void AddDynamicCost(int centerX, int centerY, int radius, double extra) {
        if (radius <= 0 || extra <= 0.0) return;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                int nx = centerX + dx;
                int ny = centerY + dy;
                if (!InBounds(nx, ny)) continue;
                double dist2 = static_cast<double>(dx * dx + dy * dy);
                if (dist2 > (radius * radius)) continue;
                dynamicCost[ny][nx] = std::min(20.0, dynamicCost[ny][nx] + extra);
            }
        }
    }

    void DecayDynamicCosts(double decayFactor) {
        if (decayFactor < 0.0) decayFactor = 0.0;
        if (decayFactor > 1.0) decayFactor = 1.0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                dynamicCost[y][x] *= decayFactor;
                if (dynamicCost[y][x] < 0.01) {
                    dynamicCost[y][x] = 0.0;
                }
            }
        }
    }

    // Check if a cell is occupied by another NPC
    bool IsOccupiedByNPC(int x, int y,
        const std::vector<NPC*>& teamBlue,
        const std::vector<NPC*>& teamOrange,
        NPC* self) // new parameter: the NPC that is moving
    {
        (void)teamBlue;
        (void)teamOrange;
        int ignoreId = -1;
        if (self) {
            ignoreId = self->GetId();
        }
        return IsOccupied(x, y, ignoreId);
    }

    bool FindNearestFreeTile(int x, int y, int radius, int& outX, int& outY, NPC* self)
    {
        if (!InBounds(x, y) || radius < 0) return false;

        struct Node { int x; int y; int dist; };
        std::queue<Node> q;
        std::vector<char> visited(W * H, 0);
        auto indexOf = [](int px, int py) { return py * W + px; };

        q.push({ x, y, 0 });
        visited[indexOf(x, y)] = 1;

        static const int OFFSETS[8][2] = {
            {1,0},{-1,0},{0,1},{0,-1},
            {1,1},{1,-1},{-1,1},{-1,-1}
        };

        while (!q.empty()) {
            Node cur = q.front();
            q.pop();

            if (cur.dist > radius) continue;

            if (IsWalkable(cur.x, cur.y) &&
                !IsOccupied(cur.x, cur.y, self ? self->GetId() : -1)) {
                outX = cur.x;
                outY = cur.y;
                return true;
            }

            for (const auto& offset : OFFSETS) {
                int nx = cur.x + offset[0];
                int ny = cur.y + offset[1];
                if (!InBounds(nx, ny)) continue;
                int idxNeighbor = indexOf(nx, ny);
                if (visited[idxNeighbor]) continue;
                visited[idxNeighbor] = 1;
                q.push({ nx, ny, cur.dist + 1 });
            }
        }
        return false;
    }


    // Shapes
    void StampSquare(double cx, double cy, double size, Cell c) {
        int minx = std::max(0, (int)std::floor(cx - size / 2.0));
        int maxx = std::min(W - 1, (int)std::ceil(cx + size / 2.0));
        int miny = std::max(0, (int)std::floor(cy - size / 2.0));
        int maxy = std::min(H - 1, (int)std::ceil(cy + size / 2.0));
        for (int y = miny; y <= maxy; ++y)
            for (int x = minx; x <= maxx; ++x)
                Set(x, y, c);
    }

    void StampEllipse(double cx, double cy, double rx, double ry, Cell c) {
        int minx = std::max(0, (int)std::floor(cx - rx - 1));
        int maxx = std::min(W - 1, (int)std::ceil(cx + rx + 1));
        int miny = std::max(0, (int)std::floor(cy - ry - 1));
        int maxy = std::min(H - 1, (int)std::ceil(cy + ry + 1));
        for (int y = miny; y <= maxy; ++y)
            for (int x = minx; x <= maxx; ++x) {
                double nx = (x - cx) / rx, ny = (y - cy) / ry;
                if (nx * nx + ny * ny <= 1.0) Set(x, y, c);
            }
    }

    // Map generation (matches your drawing)
    void BuildLogicalMapLikeYourDrawField() {
        Init();

        // Trees (passable but block sight & bullets)
        Set(70, 73, TREE); Set(76, 78, TREE);
        Set(156, 46, TREE);
        Set(76, 38, TREE); Set(86, 42, TREE); Set(92, 36, TREE);
        Set(112, 68, TREE); Set(118, 74, TREE); Set(124, 80, TREE);
        Set(140, 28, TREE); Set(148, 26, TREE);

        // Rocks (solid)
        StampSquare(45, 65, 6, ROCK);
        StampSquare(51, 60, 6, ROCK);
        StampSquare(132, 50, 6, ROCK);
        StampSquare(132, 45, 6, ROCK);
        StampSquare(32, 37, 6, ROCK);
        StampSquare(150, 64, 6, ROCK);
        StampSquare(102, 40, 6, ROCK);
        StampSquare(118, 32, 6, ROCK);
        StampSquare(172, 48, 6, ROCK);

        // Water (not walkable but bullets & LoS pass)
        StampEllipse(98, 65, 10, 5, WATER);
        StampEllipse(55, 36, 8, 5, WATER);

        // Warehouses (solid, block LoS)
        StampSquare(20, 85, 5, WAREHOUSE);   // top-left ammo
        StampSquare(15, 20, 5, WAREHOUSE);   // bottom-left medical
        StampSquare(180, 85, 5, WAREHOUSE);  // top-right ammo
        StampSquare(185, 20, 5, WAREHOUSE);  // bottom-right medical

        // Make surroundings of bottom warehouses walkable
        for (int dx = -3; dx <= 3; ++dx)
            for (int dy = -3; dy <= 3; ++dy) {
                int x1 = 15 + dx, y1 = 15 + dy;
                int x2 = 185 + dx, y2 = 15 + dy;
                if (InBounds(x1, y1) && Get(x1, y1) != ROCK && Get(x1, y1) != WATER) Set(x1, y1, FREE);
                if (InBounds(x2, y2) && Get(x2, y2) != ROCK && Get(x2, y2) != WATER) Set(x2, y2, FREE);
            }

        // Ensure some FREE cells inside the medical warehouses as entrance
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy) {
                int x1 = 15 + dx, y1 = 15 + dy;
                int x2 = 185 + dx, y2 = 15 + dy;
                if (InBounds(x1, y1)) Set(x1, y1, FREE);
                if (InBounds(x2, y2)) Set(x2, y2, FREE);
            }
    }

    // Warehouses
    WarehouseInfo GetWarehouseForTeam(TeamId team) {
        WarehouseInfo wh;
        if (team == TeamId::Orange) {
            wh.ammoX = 28;  wh.ammoY = 80;   // walkable entry just outside ammo depot
            wh.medX = 15;   wh.medY = 15;
        }
        else {
            wh.ammoX = 172; wh.ammoY = 80;   // walkable entry near blue ammo depot
            wh.medX = 185; wh.medY = 15;
        }
        return wh;
    }

    // Trees, Rocks, Warehouses block; Water does NOT block.
    bool IsLineOfSightClear(int x1, int y1, int x2, int y2) {
        int dx = std::abs(x2 - x1), dy = std::abs(y2 - y1);
        int sx = (x1 < x2) ? 1 : -1;
        int sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;

        while (true) {
            Cell c = Get(x1, y1);
            if (c == ROCK || c == TREE || c == WAREHOUSE) return false;
            if (x1 == x2 && y1 == y2) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x1 += sx; }
            if (e2 < dx) { err += dx; y1 += sy; }
        }
        return true;
    }

    // Security map (danger heatmap)
    void ResetSecurityMaps() {
        for (int t = 0; t < 2; ++t)
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    securityMaps[t][y][x] = 0.0;
    }

    // Casts multiple rays from an enemy and accumulates danger values.
    static void AddRaycastFromShooterInternal(int sx, int sy, int numRays, int fireRange, double increment,
        double(*securityMapTeam)[W])
    {
        double startX = sx + 0.5;
        double startY = sy + 0.5;

        for (int r = 0; r < numRays; ++r) {
            double ang = (2.0 * M_PI * r) / (double)numRays;
            double dx = std::cos(ang);
            double dy = std::sin(ang);

            double fx = startX;
            double fy = startY;

            for (int step = 0; step < fireRange; ++step) {
                fx += dx;
                fy += dy;
                int tx = (int)std::floor(fx);
                int ty = (int)std::floor(fy);
                if (!InBounds(tx, ty)) break;

                Cell c = Get(tx, ty);

                // ROCK: stop ray; mark strong danger on that cell
                if (c == ROCK) {
                    securityMapTeam[ty][tx] = std::min(1.0, securityMapTeam[ty][tx] + increment * 2.0);
                    break;
                }

                // TREE or WAREHOUSE: stop ray; mark medium danger
                if (c == TREE || c == WAREHOUSE) {
                    securityMapTeam[ty][tx] = std::min(1.0, securityMapTeam[ty][tx] + increment * 1.5);
                    break;
                }

                // WATER & FREE: bullets pass; accumulate normal danger
                securityMapTeam[ty][tx] = std::min(1.0, securityMapTeam[ty][tx] + increment);
            }
        }
    }

    // Public API: keep your header signature.
    void AddFireRiskFromEnemy(int ex, int ey, int fireRange, TeamId targetTeam) {
        // reasonable defaults aligned with the lecturer's demo feel
        const int numRays = 72;
        const double increment = 0.02;
        if (!InBounds(ex, ey)) return;
        double(*teamMap)[W] = securityMaps[TeamIndex(targetTeam)];
        AddRaycastFromShooterInternal(ex, ey, numRays, fireRange, increment, teamMap);
    }

    void AddFireRiskAt(int ex, int ey, TeamId targetTeam, double increment) {
        if (!InBounds(ex, ey)) return;
        double(*teamMap)[W] = securityMaps[TeamIndex(targetTeam)];
        teamMap[ey][ex] = std::min(1.0, teamMap[ey][ex] + increment);
    }

    void BuildSecurityMap(const std::vector<NPC*>& enemies, TeamId targetTeam) {
        const int defaultRange = 35; // tweakable
        for (auto e : enemies) {
            if (!e || !e->IsAlive()) continue;
            if (e->getRole() != Role::Warrior) continue;
            int sx = (int)std::floor(e->getX());
            int sy = (int)std::floor(e->getY());
            if (!InBounds(sx, sy)) continue;
            AddFireRiskFromEnemy(sx, sy, defaultRange, targetTeam);
        }
    }

    // White = safe (0.0), Black = dangerous (>=1.0), terrain colors preserved.
    void DrawSecurityMap(TeamId team) {
        double(*teamMap)[W] = securityMaps[TeamIndex(team)];
        TeamId otherTeam = (team == TeamId::Orange) ? TeamId::Blue : TeamId::Orange;
        double(*otherMap)[W] = securityMaps[TeamIndex(otherTeam)];
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {

                Cell c = Get(x, y);
                if (c == ROCK) {
                    glColor3d(1.0, 0.0, 1.0); // purple for rocks
                }
                else if (c == TREE) {
                    glColor3d(0.0, 0.5, 0.0); // green for trees
                }
                else if (c == WATER) {
                    glColor3d(0.3, 0.6, 0.9); // blue for water
                }
                else if (c == WAREHOUSE) {
                    glColor3d(1.0, 1.0, 0.0); // yellow for warehouse
                }
                else {
                    double dangerPrimary = std::min(1.0, std::max(0.0, teamMap[y][x]));
                    double dangerSecondary = std::min(1.0, std::max(0.0, otherMap[y][x]));
                    double combined = std::max(dangerPrimary, dangerSecondary);

                    double r = 1.0 - combined;
                    double g = 1.0 - combined;
                    double b = 1.0 - combined;

                    const double eps = 1e-5;
                    if (dangerPrimary > dangerSecondary + eps) {
                        if (team == TeamId::Orange) {
                            g = std::max(0.0, g - dangerPrimary * 0.12);
                            b = std::max(0.0, b - dangerPrimary * 0.20);
                        }
                        else {
                            r = std::max(0.0, r - dangerPrimary * 0.18);
                        }
                    }
                    else if (dangerSecondary > dangerPrimary + eps) {
                        if (team == TeamId::Orange) {
                            r = std::max(0.0, r - dangerSecondary * 0.18);
                        }
                        else {
                            g = std::max(0.0, g - dangerSecondary * 0.12);
                            b = std::max(0.0, b - dangerSecondary * 0.20);
                        }
                    }

                    glColor3d(r, g, b);
                }

                glBegin(GL_POLYGON);
                glVertex2d(x, y);
                glVertex2d(x, y + 1);
                glVertex2d(x + 1, y + 1);
                glVertex2d(x + 1, y);
                glEnd();
            }
        }
    }

    // Visibility map 
    void UpdateVisibilityMap(const std::vector<NPC*>& team) {
        // reset visibility
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                visibilityMap[y][x] = 0.0;

        // cast short rays from each teammate
        const int numRays = 72;
        const int maxRange = 30;
        for (auto a : team) {
            if (!a || !a->IsAlive()) continue;
            int sx = (int)std::floor(a->getX());
            int sy = (int)std::floor(a->getY());
            if (!InBounds(sx, sy)) continue;

            double startX = sx + 0.5;
            double startY = sy + 0.5;

            for (int r = 0; r < numRays; ++r) {
                double ang = (2.0 * M_PI * r) / (double)numRays;
                double dx = std::cos(ang);
                double dy = std::sin(ang);

                double fx = startX;
                double fy = startY;

                for (int step = 0; step < maxRange; ++step) {
                    fx += dx;
                    fy += dy;
                    int tx = (int)std::floor(fx);
                    int ty = (int)std::floor(fy);
                    if (!InBounds(tx, ty)) break;

                    Cell c = Get(tx, ty);
                    visibilityMap[ty][tx] = 1.0; // mark visible

                    if (c == ROCK || c == TREE || c == WAREHOUSE) break; // stop at blockers
                }
            }
        }
    }

    void DrawVisibilityMap() {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                double vis = std::min(1.0, std::max(0.0, visibilityMap[y][x]));
                // dim background according to visibility (0=dark, 1=bright)
                glColor3d(0.15 + 0.85 * vis, 0.15 + 0.85 * vis, 0.15 + 0.85 * vis);

                glBegin(GL_POLYGON);
                glVertex2d(x, y);
                glVertex2d(x, y + 1);
                glVertex2d(x + 1, y + 1);
                glVertex2d(x + 1, y);
                glEnd();
            }
        }
    }

    // Small helper if you need raw values elsewhere
    double GetSecurityValue(int y, int x, TeamId team) {
        if (!InBounds(x, y)) return 0.0;
        return securityMaps[TeamIndex(team)][y][x];
    }

    double GetVisibilityValue(int y, int x) {
        if (!InBounds(x, y)) return 0.0;
        return visibilityMap[y][x];
    }
}
