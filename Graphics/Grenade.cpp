#include "Grenade.h"
#include "glut.h"
#include <math.h>
#include <time.h>
#include <algorithm>

Grenade::Grenade(double posX, double posY)
{
    int i;
    double alpha, teta = 2 * M_PI / NUM_BULLETS;
    x = posX;
    y = posY;

    for (i = 0, alpha = 0; i < NUM_BULLETS; i++, alpha += teta)
    {
        bullets[i] = new Bullet(x, y, alpha);
    }

    isExploding = false;
    explosionStartTime = 0.0;
}

Grenade::~Grenade()
{
    for (int i = 0; i < NUM_BULLETS; i++)
    {
        delete bullets[i];
    }
}

void Grenade::Show() const
{
    int i;
    for (i = 0; i < NUM_BULLETS; i++)
        if (bullets[i]->GetIsMoving())
            bullets[i]->Show();
}

void Grenade::Explode()
{
    int i;
    for (i = 0; i < NUM_BULLETS; i++)
        bullets[i]->Move();
}

void Grenade::SetIsExploding(bool value)
{
    isExploding = value;
    int i;
    if (value) {
        explosionStartTime = clock() / (double)CLOCKS_PER_SEC;
    }
    for (i = 0; i < NUM_BULLETS; i++)
        bullets[i]->SetIsMoving(value);
}

void Grenade::CreateSecurityMap()
{
    int i;
    for (i = 0; i < NUM_BULLETS; i++)
        bullets[i]->CreateSecurityMap();
}


