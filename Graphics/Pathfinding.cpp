
#include "Pathfinding.h"
#include "Map.h"
#include "Roles.h"
#include <queue>
#include <cmath>
#include <limits>
#include <algorithm>
#include <stdio.h>

namespace Path
{
    //  4-direction movement
    static inline double Heuristic(int x1, int y1, int x2, int y2)
    {
        return std::abs(x1 - x2) + std::abs(y1 - y2);
    }

    // Convert (x,y) to 1D index
    static inline int idx(int x, int y)
    {
        return y * Map::W + x;
    }

    struct Node
    {
        int x, y;
        double g; // cost from start
        double f; // g + heuristic
        bool operator<(const Node& other) const { return f > other.f; } // min-heap
    };

    // Reconstruct path from 'came' chain
    static void Reconstruct(int sx, int sy, int gx, int gy,
        const std::vector<int>& came,
        std::vector<std::pair<int, int>>& out)
    {
        out.clear();
        int cur = idx(gx, gy);
        int start = idx(sx, sy);
        while (cur != -1)
        {
            int x = cur % Map::W;
            int y = cur / Map::W;
            out.emplace_back(x, y);
            if (cur == start) break;
            cur = came[cur];
        }
        std::reverse(out.begin(), out.end());
    }

bool FindPath(int sx, int sy, int gx, int gy, std::vector<std::pair<int, int>>& out, int ignoreNpcId)
    {
        //  Basic guards 
        if (!Map::InBounds(sx, sy) || !Map::InBounds(gx, gy)) {
            printf("? Pathfinding: start or goal out of bounds.\n");
            return false;
        }
        if (!Map::IsWalkable(sx, sy)) {
            printf(" Pathfinding: start not walkable (%d,%d)\n", sx, sy);
            return false;
        }
        if (!Map::IsWalkable(gx, gy)) {
        printf(" Pathfinding: goal not walkable (%d,%d) - will try nearby.\n", gx, gy);
        }

        const int N = Map::W * Map::H;
        const double INF = std::numeric_limits<double>::infinity();

        std::vector<double> gscore(N, INF);
        std::vector<double> fscore(N, INF);
        std::vector<int> came(N, -1);
        std::vector<char> closed(N, 0);

        std::priority_queue<Node> open;

        int s = idx(sx, sy);
        int g = idx(gx, gy);

        gscore[s] = 0.0;
        fscore[s] = Heuristic(sx, sy, gx, gy);
        open.push({ sx, sy, 0.0, fscore[s] });

        const int DX[4] = { +1, -1, 0, 0 };
        const int DY[4] = { 0, 0, +1, -1 };

        while (!open.empty())
        {
            Node cur = open.top();
            open.pop();

            int ci = idx(cur.x, cur.y);
            if (closed[ci]) continue;
            closed[ci] = 1;

            if (cur.x == gx && cur.y == gy)
            {
                Reconstruct(sx, sy, gx, gy, came, out);
                printf("  Path found! length = %zu (from %d,%d to %d,%d)\n",
                    out.size(), sx, sy, gx, gy);
                return true;
            }

            for (int k = 0; k < 4; ++k)
            {
                int nx = cur.x + DX[k];
                int ny = cur.y + DY[k];
                if (!Map::InBounds(nx, ny)) continue;
                if (!Map::IsWalkable(nx, ny)) continue;

                int ni = idx(nx, ny);
                if (closed[ni]) continue;

                double occupancyPenalty = Map::GetOccupancyPenalty(nx, ny, ignoreNpcId);
                double tentative = gscore[ci] + 1.0 + occupancyPenalty + Map::GetDynamicCost(nx, ny);

                if (tentative < gscore[ni])
                {
                    came[ni] = ci;
                    gscore[ni] = tentative;
                    fscore[ni] = tentative + Heuristic(nx, ny, gx, gy);
                    open.push({ nx, ny, gscore[ni], fscore[ni] });
                }
            }
        }

        //  No path found: try nearby cells as fallback 
        printf(" No path found from (%d,%d) to (%d,%d). Trying nearby cells...\n",
            sx, sy, gx, gy);

        double bestDist = 1e9;
        int bestX = gx, bestY = gy;

        for (int dy = -3; dy <= 3; ++dy)
        {
            for (int dx = -3; dx <= 3; ++dx)
            {
                int nx = gx + dx;
                int ny = gy + dy;
                if (!Map::InBounds(nx, ny)) continue;
                if (!Map::IsWalkable(nx, ny)) continue;

                double d2 = dx * dx + dy * dy;
                if (d2 < bestDist)
                {
                    bestDist = d2;
                    bestX = nx;
                    bestY = ny;
                }
            }
        }

        if (bestX != gx || bestY != gy)
        {
            printf(" Trying alternate goal near (%d,%d) -> (%d,%d)\n", gx, gy, bestX, bestY);
            return FindPath(sx, sy, bestX, bestY, out, ignoreNpcId);
        }

        printf(" No reachable area found around (%d,%d)\n", gx, gy);
        return false;
    }

    // BFS to find nearest safe cover point
    bool FindNearestCover(int sx, int sy, int searchRadius, TeamId team, std::pair<int, int>& out)
    {
        if (!Map::InBounds(sx, sy)) return false;

        struct BFSNode {
            int x, y;
            int dist;
        };

        std::queue<BFSNode> q;
        std::vector<char> visited(Map::W * Map::H, 0);
        
        const int DX[8] = { +1, -1, 0, 0, +1, +1, -1, -1 };  // 4-direction + diagonals
        const int DY[8] = { 0, 0, +1, -1, +1, -1, +1, -1 };
        
        double bestSafety = 1.0;  // Lower is safer
        int bestX = -1, bestY = -1;
        int bestDist = 999999;

        q.push({ sx, sy, 0 });
        visited[idx(sx, sy)] = 1;

        bool hasFallback = false;
        double fallbackSafety = 1.0;
        int fallbackX = sx;
        int fallbackY = sy;

        while (!q.empty())
        {
            BFSNode cur = q.front();
            q.pop();

            if (cur.dist > searchRadius) continue;

            // Check if this cell is a good cover point
            if (Map::IsWalkable(cur.x, cur.y))
            {
                double security = Map::GetSecurityValue(cur.y, cur.x, team);
                double occupancyPenalty = Map::GetOccupancyPenalty(cur.x, cur.y);
                double adjustedSafety = security + 0.05 * occupancyPenalty;
                // Prefer points with low security (safe) and near cover (behind trees/rocks)
                bool nearCover = false;
                bool isStart = (cur.x == sx && cur.y == sy);
                
                // Check if near a tree or rock (good cover)
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        int nx = cur.x + dx;
                        int ny = cur.y + dy;
                        if (Map::InBounds(nx, ny)) {
                            Map::Cell c = Map::Get(nx, ny);
                            if (c == Map::TREE || c == Map::ROCK) {
                                nearCover = true;
                                break;
                            }
                        }
                    }
                    if (nearCover) break;
                }

                // Good cover point: low security, near cover, not too far
                if (adjustedSafety < 0.3 && (nearCover || adjustedSafety < 0.1))
                {
                    if (isStart) {
                        if (!hasFallback || adjustedSafety < fallbackSafety) {
                            hasFallback = true;
                            fallbackSafety = adjustedSafety;
                            fallbackX = cur.x;
                            fallbackY = cur.y;
                        }
                    }
                    else if (adjustedSafety < bestSafety || (adjustedSafety == bestSafety && cur.dist < bestDist))
                    {
                        bestSafety = adjustedSafety;
                        bestX = cur.x;
                        bestY = cur.y;
                        bestDist = cur.dist;
                    }
                }
            }

            // Expand neighbors
            for (int k = 0; k < 4; ++k)  // Only 4-direction for BFS
            {
                int nx = cur.x + DX[k];
                int ny = cur.y + DY[k];
                if (!Map::InBounds(nx, ny)) continue;
                
                int ni = idx(nx, ny);
                if (visited[ni]) continue;
                if (!Map::IsWalkable(nx, ny)) continue;

                visited[ni] = 1;
                q.push({ nx, ny, cur.dist + 1 });
            }
        }

        if (bestX != -1 && bestY != -1)
        {
            out = { bestX, bestY };
            printf("  Found cover point at (%d,%d) with safety=%.2f\n", bestX, bestY, bestSafety);
            return true;
        }

        if (hasFallback) {
            out = { fallbackX, fallbackY };
            printf("  Using fallback cover at (%d,%d) with safety=%.2f\n", fallbackX, fallbackY, fallbackSafety);
            return true;
        }

        printf("  No cover point found within radius %d\n", searchRadius);
        return false;
    }

    // A* with security map consideration
    bool FindSafePath(int sx, int sy, int gx, int gy, TeamId team, std::vector<Cell>& out, double securityWeight, int ignoreNpcId)
    {
        // Similar to FindPath but adds security cost to edge weights
        if (!Map::InBounds(sx, sy) || !Map::InBounds(gx, gy)) {
            return false;
        }
        if (!Map::IsWalkable(sx, sy)) {
            return false;
        }

        const int N = Map::W * Map::H;
        const double INF = std::numeric_limits<double>::infinity();

        std::vector<double> gscore(N, INF);
        std::vector<double> fscore(N, INF);
        std::vector<int> came(N, -1);
        std::vector<char> closed(N, 0);

        std::priority_queue<Node> open;

        int s = idx(sx, sy);
        int g = idx(gx, gy);

        gscore[s] = 0.0;
        fscore[s] = Heuristic(sx, sy, gx, gy);
        open.push({ sx, sy, 0.0, fscore[s] });

        const int DX[4] = { +1, -1, 0, 0 };
        const int DY[4] = { 0, 0, +1, -1 };

        while (!open.empty())
        {
            Node cur = open.top();
            open.pop();

            int ci = idx(cur.x, cur.y);
            if (closed[ci]) continue;
            closed[ci] = 1;

            if (cur.x == gx && cur.y == gy)
            {
                Reconstruct(sx, sy, gx, gy, came, out);
                return true;
            }

            for (int k = 0; k < 4; ++k)
            {
                int nx = cur.x + DX[k];
                int ny = cur.y + DY[k];
                if (!Map::InBounds(nx, ny)) continue;
                if (!Map::IsWalkable(nx, ny)) continue;

                int ni = idx(nx, ny);
                if (closed[ni]) continue;

                // Base cost + security penalty
                double security = Map::GetSecurityValue(ny, nx, team);
                double occupancyPenalty = Map::GetOccupancyPenalty(nx, ny, ignoreNpcId);
                double extraCost = Map::GetDynamicCost(nx, ny);
                double edgeCost = 1.0 + securityWeight * security * 10.0 + occupancyPenalty + extraCost;
                double tentative = gscore[ci] + edgeCost;

                if (tentative < gscore[ni])
                {
                    came[ni] = ci;
                    gscore[ni] = tentative;
                    fscore[ni] = tentative + Heuristic(nx, ny, gx, gy);
                    open.push({ nx, ny, gscore[ni], fscore[ni] });
                }
            }
        }

        return false;
    }
}
