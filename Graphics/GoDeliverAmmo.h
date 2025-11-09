#pragma once
#include "State.h"
#include <vector>

class NPC;

class GoDeliverAmmo : public State {
private:
    NPC* targetLowAmmo;
    double targetX;
    double targetY;
    bool waitingToDeliver;
    double lastDistanceCheckTime;
    double lastDistanceToTarget;

public:
    explicit GoDeliverAmmo(NPC* explicitTarget = nullptr)
        : targetLowAmmo(explicitTarget),
        targetX(0.0),
        targetY(0.0),
        waitingToDeliver(false),
        lastDistanceCheckTime(0.0),
        lastDistanceToTarget(0.0) {}

    void OnEnter(NPC* pn) override;
    void Transition(NPC* pn) override;
    void OnExit(NPC* pn) override;
};
