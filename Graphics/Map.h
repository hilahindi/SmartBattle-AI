#pragma once
#include <vector>
#include "Roles.h"

class NPC; // forward declaration

namespace Map {

    // Cell types 
    enum Cell : unsigned char {
        FREE = 0,        // walkable, line of sight passes
        TREE = 1,        // walkable, BUT blocks bullets & vision
        ROCK = 2,        // not walkable, blocks bullets & vision
        WATER = 3,       // not walkable, bullets & vision pass
        WAREHOUSE = 4,   // not walkable, blocks bullets & vision
        TEMP_OCCUPIED = 5
    };

    // World size
    static const int W = 200;
    static const int H = 100;

    // Basic map operations
    void Init();
    Cell Get(int x, int y);
    void Set(int x, int y, Cell c);
    bool InBounds(int x, int y);
    bool IsWalkable(int x, int y);
    bool IsLineOfSightClear(int x1, int y1, int x2, int y2);

    // Drawing / shape helpers
    void StampSquare(double cx, double cy, double size, Cell c);
    void StampEllipse(double cx, double cy, double rx, double ry, Cell c);

    // Logical map builder
    void BuildLogicalMapLikeYourDrawField();

    // Warehouses
    struct WarehouseInfo { int ammoX, ammoY; int medX, medY; };
    WarehouseInfo GetWarehouseForTeam(TeamId t);

    // Dynamic occupancy tracking
    bool IsOccupied(int x, int y, int ignoreNpcId = -1);
    void SetOccupied(int x, int y, int byNpcId);
    void ClearOccupied(int x, int y, int byNpcId);
    double GetOccupancyPenalty(int x, int y, int ignoreNpcId = -1);

    // Dynamic path cost adjustments
    double GetDynamicCost(int x, int y);
    void AddDynamicCost(int centerX, int centerY, int radius, double extra);
    void DecayDynamicCosts(double decayFactor);

    // Security map (danger heatmap)
    void ResetSecurityMaps();
    // Adds danger around a single enemy using raycasts; fireRange is in cells
    void AddFireRiskFromEnemy(int ex, int ey, int fireRange, TeamId targetTeam);
    // Adds small danger value at a specific cell (for bullets/grenades)
    void AddFireRiskAt(int ex, int ey, TeamId targetTeam, double increment = 0.001);
    // Builds the whole security map from a list of enemies (or shooters)
    void BuildSecurityMap(const std::vector<NPC*>& enemies, TeamId targetTeam);
    // Draws terrain + grayscale danger (white=safe, black=danger)
    void DrawSecurityMap(TeamId team = TeamId::Orange);

    // Visibility map (line of sight)
    void UpdateVisibilityMap(const std::vector<NPC*>& team);
    void DrawVisibilityMap();
    // Get visibility value at a cell (0.0 = not visible, 1.0 = visible)
    double GetVisibilityValue(int y, int x);

    // Optional small helper (if you need raw values elsewhere)
    double GetSecurityValue(int y, int x, TeamId team);

    bool IsOccupiedByNPC(int x, int y,
        const std::vector<NPC*>& teamBlue,
        const std::vector<NPC*>& teamOrange,
        NPC* self);
    bool FindNearestFreeTile(int x, int y, int radius, int& outX, int& outY, NPC* self = nullptr);
}
