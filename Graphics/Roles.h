#pragma once

// --- team & role enums ---
enum class TeamId : unsigned char { Orange = 0, Blue = 1 };
enum class Role : unsigned char { Commander, Warrior, Medic, Porter };


inline char RoleLetter(Role r) {
    switch (r) {
    case Role::Commander: return 'C';
    case Role::Warrior:   return 'W';
    case Role::Medic:     return 'M';
    case Role::Porter:    return 'P';
    }
    return '?';
}

struct TeamColor { double r, g, b; };
inline TeamColor GetTeamColor(TeamId t) {
    // orange / blue to match your drawing
    return (t == TeamId::Orange) ? TeamColor{ 1.0, 0.5, 0.0 }
    : TeamColor{ 0.2, 0.6, 1.0 };
}
