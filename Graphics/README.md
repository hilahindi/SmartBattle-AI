# AI Battle Simulation

This project simulates two small squads fighting across a tactical map. Each unit runs a simple finite-state machine and uses the shared security/visibility maps to make movement decisions.

## Building
- Open `Graphics.sln` with Visual Studio 2022 (toolset v143).  
- Build the `Graphics` project in either Debug or Release configuration.
- Required runtime libraries (`freeglut`, `glew`) are already provided under `Graphics/` and copied to the Debug folder after the first build.

## Controls
- `S` – toggle the global danger (security) overlay.  
- `Right click` – quick toggle of the same security overlay.  
- `1` / `2` – show danger from the perspective of the Orange / Blue team; `0` turns the overlay off.  
- `V` – toggle the visibility overlay (line-of-sight coverage).  
- `R` – restart the match (rebuilds teams, commanders, and heatmaps).  
- Standard mouse drag / scroll via GLUT remain unchanged.

## Gameplay Cues
- Units that take damage flash with a red outline before falling back.
- Grenade detonations now show a short-lived shockwave ring in addition to shrapnel.
- Bullet tracers render a brighter double-line streak for readability.

## AI Highlights
- Warriors retreat automatically when low on health or overwhelmed and regroup toward their defensive band.  
- Idle warriors, medics, and porters receive short patrol anchors so they continue scanning nearby cover.  
- Pathfinding re-plans when allies block a route; supply and medic runs fall back to nearby cover if the main depot is obstructed.

## Quick Acceptance Checklist
- Low-ammo warriors request porters, who reach them via safe routes.  
- Medics detour around temporary blockages and still arrive at injured allies.  
- Retreat triggers move endangered warriors to cover, then back toward the defensive midline.  
- Overlays respond instantly to `S`, `V`, `0-2`, and right-click.  
- Pressing `R` restarts the simulation without restarting the executable.
*** End Patch

