#pragma once

#define MSZ 100

#define SPEED  0.06
#define M_PI     3.14159265358979323846

#ifndef SHOW_SECURITY
#define SHOW_SECURITY 0
#endif

#ifndef SHOW_VIS
#define SHOW_VIS 0
#endif

#ifndef DRAW_PATHS
#define DRAW_PATHS 0
#endif

// Combat constants
const double FIRE_RANGE = 15.0;        // Maximum shooting range (cells)
const double GRENADE_RANGE = 20.0;     // Maximum grenade throw range (cells)
const int GRENADE_DAMAGE = 18;         // Damage per grenade
const int BULLET_DAMAGE = 7;           // Damage per bullet
const int MAX_AMMO = 10;               // Maximum ammo capacity
const int MAX_GRENADES = 1;            // Maximum grenades capacity
const int LOW_AMMO_THRESHOLD = MAX_AMMO / 2;     // Report low ammo when below this
const int INJURY_THRESHOLD = 60;       // Request medic when HP drops below 60%

const int MEDIC_MAX_SUPPLIES = 1;      // Number of full heals before resupply
const int PORTER_MAX_SUPPLIES = 1;     // Number of ammo crates before resupply
const int MEDIC_HEAL_AMOUNT = 100;     // Heal brought by medic
const int PORTER_RELOAD_AMOUNT = MAX_AMMO; // Ammo restored by a single crate

const double ENEMY_REPORT_COOLDOWN = 3.0;  // Seconds between enemy spotted reports
const double ORDER_DEBOUNCE_SECONDS = 2.0; // Prevent spamming identical orders



