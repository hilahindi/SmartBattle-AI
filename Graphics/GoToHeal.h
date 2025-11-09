#pragma once
#include "State.h"

class NPC;

class GoToHeal : public State {
private:
    NPC* targetInjured;
    bool healing;
    double healStart;
    double lastDistanceCheckTime;
    double lastDistanceToTarget;

public:
    GoToHeal()
        : targetInjured(nullptr),
        healing(false),
        healStart(0.0),
        lastDistanceCheckTime(0.0),
        lastDistanceToTarget(0.0) {}

    void OnEnter(NPC* pn) override;
    void Transition(NPC* pn) override;
    void OnExit(NPC* pn) override;
};
