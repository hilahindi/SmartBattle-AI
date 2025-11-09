#pragma once
#include "State.h"
#include <vector>
#include <algorithm>
#include <utility>
#include "Roles.h"
#include "Definitions.h"

// Global list of all NPCs (used for collision checks)
extern std::vector<NPC*> allNPCs;


//enum class TeamState { ATTACK, DEFEND, RETREAT }; // for team logic reference

class Commander; // forward declaration

class NPC {
private:
    double x, y;
    double targetX, targetY;
    double dirX, dirY;
    bool isMoving, isEngaging, isDelivering, isResting, isLowAmmo;
    int ammo;      // number of bullets
    int grenades;  // number of grenades (for Warriors only)
    int hp;        // health points
    int maxAmmo;
    int supply;    // generic supply counter (medkits, ammo crates, etc.)
    int maxSupply;
    int assistsDone;
    NPC* targetNPC;

    static int nextId;

    int id;
    int occupiedCellX;
    int occupiedCellY;
    bool hasOccupancy;
    int waitTicks;
    int stuckTicks;
    double lastProgressX;
    double lastProgressY;
    bool pendingFullReplan;
    int blockCounter;

    bool hasOrderTarget;
    double orderTargetX;
    double orderTargetY;
    
    State* pCurrentState;
    State* pInterruptedState;

    // --- path following state ---
    std::vector<std::pair<int, int>> path; // grid cells to follow
    int pathIndex; // current waypoint index in 'path'

    TeamId team;
    Role role;
    double size;

    Commander* commander;  // reference to this NPC's commander

    double blockedSinceTime;
    int blockedCellX;
    int blockedCellY;
    double lastRetreatTime;
    double lastIdleAnchorTime;
    double hitFlashUntil;
    double lastBlockedTime;
    int blockedAttempts;
    int replanAttempts;
    double lastReplanAttemptTime;
    double lastEnemyReportTime;

    void ClearPathBlocking();
    bool TryPlanAroundOccupiedCell(int blockedX, int blockedY, int goalX, int goalY);
    bool PlanShortDetour(int searchRadius);
    bool TryStepAside();
    void UpdateOccupancy();
    bool ReplanPathWithDynamicCosts();


public:
    static constexpr int ASSIST_LIMIT = 1;

    NPC(double posX, double posY, TeamId t, Role r, double sz = 4.0);

    virtual ~NPC();                      // ? makes NPC polymorphic
    virtual void UpdateBehavior() {}     // optional override for subclasses


    void DoSomeWork();
    void Show();
    void setDirection();

    // --- path API ---
    void SetPath(const std::vector<std::pair<int, int>>& p); // assign whole path
    void GoToGrid(int gx, int gy); // compute A* and start moving

    int GetId() const { return id; }

    void setTarget(double tx, double ty) { targetX = tx; targetY = ty; }
    void setIsMoving(bool v) { isMoving = v; }
    void setIsEngaging(bool v) { isEngaging = v; }
    void setIsDelivering(bool v) { isDelivering = v; }
    void setIsResting(bool v) { isResting = v; }
    void setLowAmmo(bool v) { isLowAmmo = v; }
    bool isBusy() const;

    void setHasOrderTarget(bool v) { hasOrderTarget = v; }
    bool getHasOrderTarget() const { return hasOrderTarget; }
    void setOrderTarget(double tx, double ty) { orderTargetX = tx; orderTargetY = ty; hasOrderTarget = true; }
    void clearOrderTarget() { hasOrderTarget = false; }
    std::pair<int, int> getOrderTarget() const { return { (int)orderTargetX, (int)orderTargetY }; }

    void setCurrentState(State* ps) { pCurrentState = ps; }
    void setInterruptedState(State* ps) { pInterruptedState = ps; }

    bool getIsEngaging() const { return isEngaging; }
    State* getCurrentState() { return pCurrentState; }
    State* getInterruptedState() { return pInterruptedState; }
    bool   getLowAmmo() const { return isLowAmmo; }

    bool getIsMoving() const { return isMoving; }
    bool getIsResting() const { return isResting; }

    bool getIsDelivering() const { return isDelivering; }
    
    void setTargetNPC(NPC* npc) { targetNPC = npc; }
    NPC* getTargetNPC() const { return targetNPC; }
    int getBlockCounter() const { return blockCounter; }
    void setBlockCounter(int c) { blockCounter = c; }
    double getTargetX() const { return targetX; }
    double getTargetY() const { return targetY; }

    double getX() const { return x; }
    double getY() const { return y; }

    // --- combat API ---
    void setAmmo(int a) { 
        if (maxAmmo > 0)
            ammo = std::max(0, std::min(maxAmmo, a));
        else
            ammo = std::max(0, a);
        if (ammo <= LOW_AMMO_THRESHOLD) {
            isLowAmmo = true;
        }
        else {
            isLowAmmo = false;
        }
    }
    int getAmmo() const { return ammo; }
    bool CanShoot() const { return role == Role::Warrior && ammo > 0; }
    void decreaseAmmo() { if (ammo > 0) ammo--; }
    void Reload(int amount);
    void RefillAmmo();
    bool NeedsAmmo() const;
    int getMaxAmmo() const { return maxAmmo; }
    
    void setGrenades(int g) {
        if (role != Role::Warrior) {
            grenades = 0;
            return;
        }
        grenades = std::max(0, g);
        supply = std::min(maxSupply, grenades);
    }
    int getGrenades() const { return grenades; }
    bool CanThrowGrenade() const { return role == Role::Warrior && grenades > 0; }
    void decreaseGrenades() { 
        if (grenades > 0) {
            grenades--;
            if (role == Role::Warrior) {
                supply = std::max(0, grenades);
            }
        }
    }
    bool CanTakeAssist() const { return (role == Role::Medic || role == Role::Porter) && assistsDone < ASSIST_LIMIT; }
    int GetAssistsDone() const { return assistsDone; }
    void RegisterAssistCompletion();
    void ResetAssistCounter() { assistsDone = 0; }

    void setSupply(int s) { 
        if (maxSupply > 0)
            supply = std::max(0, std::min(maxSupply, s));
        else
            supply = std::max(0, s);
    }
    int getSupply() const { return supply; }
    int getMaxSupply() const { return maxSupply; }
    void consumeSupply(int amount) { 
        supply = std::max(0, supply - amount); 
        if (role == Role::Warrior) {
            grenades = supply;
        }
    }
    void addSupply(int amount) { 
        supply = std::min(maxSupply, supply + amount); 
        if (role == Role::Warrior) {
            grenades = supply;
        }
    }

    void setHP(int h) { hp = h; }
    int getHP() const { return hp; }
    bool IsAlive() const { return hp > 0; }

    int getPathSize() const { return (int)path.size(); }

    TeamId getTeam() const { return team; }
    Role   getRole() const { return role; }
    char   getSymbol() const { return RoleLetter(role); }
    double getSize() const { return size; }

    void   DrawAsSquareWithLetter() const;
    void   DrawHPBar(bool placeAbove) const;  // Draw HP bar relative to NPC
    void   DrawAmmoBar(bool placeAbove) const;
    void   DrawSupplyBar(bool placeAbove) const;
    void   DrawDeadMarker() const;
    void   DrawStatusBars() const;
    void AssignInitialStateByRole();
    double getMoveSpeed() const;

    // --- reporting and awareness API ---
    void ReportEnemySpotted(int ex, int ey);
    void ReportLowAmmo();
    void ReportInjury();
    void TakeDamage(int dmg);
    void HealSelf(int amount);


    // --- combat interaction ---
    bool CanSee(NPC* target) const;
    bool InRange(NPC* target, double range) const;
    void Shoot(NPC* target);
    void ThrowGrenade(double targetX, double targetY);  // Warriors throw grenade at position

    void setCommander(Commander* c) { commander = c; }
    Commander* getCommander() const { return commander; }

    void MarkRetreat();
    double GetLastRetreatTime() const { return lastRetreatTime; }
    void MarkIdleAnchorIssued();
    double GetLastIdleAnchorTime() const { return lastIdleAnchorTime; }

};

void UpdateActiveGunshots();
void DrawActiveGunshots();
