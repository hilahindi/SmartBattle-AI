#pragma once
#include "Definitions.h"

class Bullet {
private:
    double x, y;
    double dirX, dirY;
    bool isMoving;
    bool isCreatingSecurityMap;

public:
    Bullet(double xPos, double yPos, double alpha);
    
    void Move();
    void Show() const;
    void SetIsMoving(bool value) { isMoving = value; }
    void SetIsCreatingSecurityMap(bool value) { isCreatingSecurityMap = value; }
    void CreateSecurityMap();
    
    bool GetIsMoving() const { return isMoving; }
    double GetX() const { return x; }
    double GetY() const { return y; }
};


