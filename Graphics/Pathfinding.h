#pragma once
#include <vector>
#include <utility>
#include "Roles.h"

namespace Path
{
    // A single grid coordinate (integer cell)
    using Cell = std::pair<int, int>; // {x, y}

    // Find a path on the logical map from (sx,sy) to (gx,gy).
    // Returns true if a path was found; 
    bool FindPath(int sx, int sy, int gx, int gy, std::vector<Cell>& out, int ignoreNpcId = -1);
    
    // Find a path using A* that considers security map (prefers safer paths).
    bool FindSafePath(int sx, int sy, int gx, int gy, TeamId team, std::vector<Cell>& out, double securityWeight = 0.5, int ignoreNpcId = -1);
    
    // BFS to find nearest safe cover point within search radius.
    // Returns true if found; 
    // A safe point is one that is walkable and has low security value (behind cover).
    bool FindNearestCover(int sx, int sy, int searchRadius, TeamId team, std::pair<int, int>& out);
}
