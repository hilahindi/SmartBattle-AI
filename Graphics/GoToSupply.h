#pragma once
#include "State.h"
#include "NPC.h"

static const int SUPPLY_X = 25;
static const int SUPPLY_Y = 85;

class GoToSupply : public State {
public:
    void OnEnter(NPC* pn);
    void Transition(NPC* pn);
    void OnExit(NPC* pn);
};

