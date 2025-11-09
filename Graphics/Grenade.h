#pragma once
#include "Bullet.h"
#include "Definitions.h"

const int NUM_BULLETS = 36;  // Number of bullets that explode from grenade

class Grenade {
private:
    double x, y;
    Bullet* bullets[NUM_BULLETS];
    bool isExploding;
    double explosionStartTime;

public:
    Grenade(double posX, double posY);
    ~Grenade();
    
    void Show() const;
    void Explode();
    void SetIsExploding(bool value);
    void CreateSecurityMap();
    
    bool GetIsExploding() const { return isExploding; }
    double GetX() const { return x; }
    double GetY() const { return y; }
    double GetExplosionStartTime() const { return explosionStartTime; }
};


