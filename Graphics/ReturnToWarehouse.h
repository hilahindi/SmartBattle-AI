#pragma once

#include "State.h"
#include <limits>

class NPC;

class ReturnToWarehouse : public State {
public:
    ReturnToWarehouse(int warehouseX, int warehouseY, double patrolRadius = 4.0,
        double avoidX = std::numeric_limits<double>::quiet_NaN(),
        double avoidY = std::numeric_limits<double>::quiet_NaN());

    void OnEnter(NPC* pn) override;
    void Transition(NPC* pn) override;
    void OnExit(NPC* pn) override;

private:
    int centerX;
    int centerY;
    double patrolRadius;
    double arrivalRadius;
    bool inPatrol;
    double nextPatrolTime;
    double lastRepathCheck;

    bool retreating;
    int retreatTargetX;
    int retreatTargetY;
    bool hasAvoidPoint;
    double avoidX;
    double avoidY;

    bool PlanPathTo(NPC* pn, int gx, int gy);
    void PlanRouteToWarehouse(NPC* pn);
    bool StartRetreat(NPC* pn, double now);
    void StartPatrol(NPC* pn, double now);
    void IssuePatrolMove(NPC* pn, double now);
};

