#pragma once
#include "State.h"
class NPC;

class GoToMedSupply : public State {
public:
    void OnEnter(NPC* pn) override;     
    void Transition(NPC* pn) override;  
    void OnExit(NPC* pn) override;      
};

