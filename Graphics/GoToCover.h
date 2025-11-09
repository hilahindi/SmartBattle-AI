#pragma once
#include "State.h"
#include "NPC.h"

static const int COVER_X = 70;
static const int COVER_Y = 73;

class GoToCover : public State {
public:
    void OnEnter(NPC* pn);
    void Transition(NPC* pn);
    void OnExit(NPC* pn);
};
