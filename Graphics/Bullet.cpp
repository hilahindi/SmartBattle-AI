#include "Bullet.h"
#include "Map.h"
#include <math.h>
#include "glut.h"
#include "Roles.h"

const double BULLET_SPEED = 0.45;

Bullet::Bullet(double xPos, double yPos, double alpha)
{
    x = xPos;
    y = yPos;
    dirX = cos(alpha);
    dirY = sin(alpha);
    isMoving = false;
    isCreatingSecurityMap = false;
}

void Bullet::Move()
{
    double tmpX = x, tmpY = y;
    if (isMoving)
    {
        tmpX += BULLET_SPEED * dirX;
        tmpY += BULLET_SPEED * dirY;
        
        int tx = (int)tmpX;
        int ty = (int)tmpY;
        
        if (Map::InBounds(tx, ty) && Map::IsWalkable(tx, ty))
        {
            x = tmpX;
            y = tmpY;
        }
        else
        {
            isMoving = false;
        }
    }
}

void Bullet::Show() const
{
    glColor3d(1, 0, 0);
    glBegin(GL_POLYGON);
    glVertex2d(x - 0.5, y);
    glVertex2d(x, y + 0.5);
    glVertex2d(x + 0.5, y);
    glVertex2d(x, y - 0.5);
    glEnd();
}

void Bullet::CreateSecurityMap()
{
    double tmpX = x, tmpY = y;
    double xsm = x, ysm = y;

    isCreatingSecurityMap = true;
    while (isCreatingSecurityMap)
    {
        tmpX += BULLET_SPEED * dirX;
        tmpY += BULLET_SPEED * dirY;
        
        int tx = (int)tmpX;
        int ty = (int)tmpY;
        
        if (Map::InBounds(tx, ty) && Map::IsWalkable(tx, ty))
        {
            xsm = tmpX;
            ysm = tmpY;
            Map::AddFireRiskAt((int)xsm, (int)ysm, TeamId::Orange, 0.001);
            Map::AddFireRiskAt((int)xsm, (int)ysm, TeamId::Blue, 0.001);
        }
        else
        {
            isCreatingSecurityMap = false;
        }
    }
}

