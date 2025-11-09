#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <cmath>
#include <stdio.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>
#include "glut.h"
#include "NPC.h"
#include "Map.h"
#include <vector>
#include "Roles.h"
#include "Commander.h"
#include "Definitions.h"
#include "Grenade.h"
#include "GoToCombat.h"
#include "GoToCover.h"
#include "Pathfinding.h"
#include "GoDeliverAmmo.h"

// ================== Globals ==================
Commander* commanderOrange;
Commander* commanderBlue;

std::vector<NPC*> teamOrange;
std::vector<NPC*> teamBlue;

// Global list of active grenades (defined in NPC.cpp)
extern std::vector<Grenade*> activeGrenades;

enum class MatchState { Running, OrangeWin, BlueWin, Draw };
static MatchState matchState = MatchState::Running;
static double matchEndTime = 0.0;
static double matchStartTime = 0.0;

static bool g_showSecurity = false;
static bool g_showVisibility = false;
static TeamId g_securityOverlayTeam = TeamId::Orange;
static double lastCommanderUpdateTime = 0.0;
static bool g_randomSeeded = false;

static void CleanupSimulation();
static void SetupSimulation();
static void ResetSimulation();
static void OnKeyboard(unsigned char key, int, int);
static void OnMouse(int button, int state, int x, int y);
static bool FindLocalPatrolTarget(NPC* npc, int radius, int& outX, int& outY);
static void EnsureIdleMotion(NPC* npc, double currentTime);

// ================== Security Map Builder ==================
static void RebuildSecurityMap()
{
    Map::ResetSecurityMaps();
    Map::BuildSecurityMap(teamBlue, TeamId::Orange);  // danger for Orange team
    Map::BuildSecurityMap(teamOrange, TeamId::Blue);  // danger for Blue team
}



// ================== Team Spawns ==================
struct SpawnSpec { int x, y; char sym; };
inline Role CharToRole(char c) {
    switch (c) {
    case 'C': return Role::Commander;
    case 'W': return Role::Warrior;
    case 'M': return Role::Medic;
    case 'P': return Role::Porter;
    }
    return Role::Warrior;
}

static const SpawnSpec ORANGE_SPAWN[] = {
    {34,68,'C'},   // commander shifted slightly south to clear the choke
    {50,72,'W'},
    {42,58,'W'},
    {13,18,'M'},
    {24,82,'P'}
};
static const SpawnSpec BLUE_SPAWN[] = {
    {172,64,'C'},  // commander tucked slightly behind northern cover
    {150,70,'W'},
    {158,54,'W'},
    {187,18,'M'},
    {176,82,'P'}
};

// ================== Team creation ==================
void SpawnTeamsFromSpecs()
{
    teamOrange.reserve(5);
    for (const auto& s : ORANGE_SPAWN) {
        Role role = CharToRole(s.sym);
        NPC* npc = new NPC(s.x, s.y, TeamId::Orange, role, 4.0);
        teamOrange.push_back(npc);
    }

    teamBlue.reserve(5);
    for (const auto& s : BLUE_SPAWN) {
        Role role = CharToRole(s.sym);
        NPC* npc = new NPC(s.x, s.y, TeamId::Blue, role, 4.0);
        teamBlue.push_back(npc);
    }

    // Assign commander reference to each NPC
    for (NPC* npc : teamOrange) npc->setCommander(commanderOrange);
    for (NPC* npc : teamBlue) npc->setCommander(commanderBlue);

}

// ================== Initialization ==================
void init()
{
    glClearColor(0.5, 0.8, 0.5, 0);
    glOrtho(0, 200, 0, 100, -1, 1);

    ResetSimulation();
}

static void CleanupTeam(std::vector<NPC*>& team)
{
    for (NPC* npc : team) {
        delete npc;
    }
    team.clear();
}

static void CleanupActiveGrenades()
{
    for (Grenade* grenade : activeGrenades) {
        delete grenade;
    }
    activeGrenades.clear();
}

static void CleanupSimulation()
{
    CleanupTeam(teamOrange);
    CleanupTeam(teamBlue);

    delete commanderOrange;
    commanderOrange = nullptr;
    delete commanderBlue;
    commanderBlue = nullptr;

    CleanupActiveGrenades();
}

static void SetupSimulation()
{
    if (!g_randomSeeded) {
        unsigned int seed = (unsigned int)time(nullptr);
        printf("[INIT] Random seed = %u\n", seed);
        srand(seed);
        g_randomSeeded = true;
    }

    Map::BuildLogicalMapLikeYourDrawField();
    SpawnTeamsFromSpecs();

    teamOrange[2]->setAmmo(1);
    printf(" Simulated low ammo: Orange W Ammo = %d\n", teamOrange[2]->getAmmo());

    commanderOrange = new Commander(teamOrange[0], teamOrange);
    commanderBlue = new Commander(teamBlue[0], teamBlue);

    for (NPC* npc : teamOrange) npc->setCommander(commanderOrange);
    for (NPC* npc : teamBlue)  npc->setCommander(commanderBlue);

    for (NPC* npc : teamOrange) {
        if (npc) npc->AssignInitialStateByRole();
    }
    for (NPC* npc : teamBlue) {
        if (npc) npc->AssignInitialStateByRole();
    }

    // Ensure initial ammo delivery kicks off immediately
    NPC* initialLowAmmoWarrior = nullptr;
    NPC* initialPorter = nullptr;
    for (NPC* npc : teamOrange) {
        if (!npc) continue;
        if (npc->getRole() == Role::Warrior && npc->NeedsAmmo()) {
            initialLowAmmoWarrior = npc;
        }
        else if (npc->getRole() == Role::Porter) {
            initialPorter = npc;
        }
    }
    if (initialLowAmmoWarrior && initialPorter) {
        State* current = initialPorter->getCurrentState();
        if (current) {
            current->OnExit(initialPorter);
            delete current;
        }
        printf("[INIT] Porter %c assigned immediate delivery to warrior %c.\n",
            initialPorter->getSymbol(), initialLowAmmoWarrior->getSymbol());
        initialPorter->setCurrentState(new GoDeliverAmmo(initialLowAmmoWarrior));
        initialPorter->getCurrentState()->OnEnter(initialPorter);
    }

    printf("=== INIT COMPLETE ===\n");
    commanderOrange->PlanAndAssignOrders();
    commanderBlue->PlanAndAssignOrders();

    // Build initial security map
    RebuildSecurityMap();

    matchStartTime = clock() / (double)CLOCKS_PER_SEC;
    matchEndTime = 0.0;
    matchState = MatchState::Running;
    lastCommanderUpdateTime = 0.0;

    Map::UpdateVisibilityMap(teamOrange);
    Map::UpdateVisibilityMap(teamBlue);
}

static void ResetSimulation()
{
    CleanupSimulation();
    g_showSecurity = SHOW_SECURITY != 0;
    g_showVisibility = SHOW_VIS != 0;
    g_securityOverlayTeam = TeamId::Orange;
    SetupSimulation();
    glutPostRedisplay();
}

// ================== Helpers ==================
static void DrawString(double x, double y, const std::string& text, const TeamColor& color = { 1.0,1.0,1.0 })
{
    glColor3d(color.r, color.g, color.b);
    glRasterPos2d(x, y);
    for (char c : text) {
        glutBitmapCharacter(GLUT_BITMAP_8_BY_13, c);
    }
}

static int CountAlive(const std::vector<NPC*>& team)
{
    int alive = 0;
    for (NPC* npc : team) {
        if (npc && npc->IsAlive()) alive++;
    }
    return alive;
}

static int CountAliveByRole(const std::vector<NPC*>& team, Role role)
{
    int alive = 0;
    for (NPC* npc : team) {
        if (npc && npc->IsAlive() && npc->getRole() == role) alive++;
    }
    return alive;
}

static void DrawHud()
{
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    TeamColor orangeColor = GetTeamColor(TeamId::Orange);
    TeamColor blueColor = GetTeamColor(TeamId::Blue);

    double baseY = 96.0;
    DrawString(2.0, baseY, "=== Match Status ===");

    std::ostringstream osOrange;
    osOrange << "Orange Alive: " << CountAlive(teamOrange)
        << " (W:" << CountAliveByRole(teamOrange, Role::Warrior)
        << " M:" << CountAliveByRole(teamOrange, Role::Medic)
        << " P:" << CountAliveByRole(teamOrange, Role::Porter) << ")";
    DrawString(2.0, baseY - 4.0, osOrange.str(), orangeColor);

    std::ostringstream osBlue;
    osBlue << "Blue Alive: " << CountAlive(teamBlue)
        << " (W:" << CountAliveByRole(teamBlue, Role::Warrior)
        << " M:" << CountAliveByRole(teamBlue, Role::Medic)
        << " P:" << CountAliveByRole(teamBlue, Role::Porter) << ")";
    DrawString(2.0, baseY - 8.0, osBlue.str(), blueColor);

    if (matchState != MatchState::Running) {
        std::string result;
        TeamColor resultColor{ 1.0,1.0,0.2 };
        switch (matchState) {
        case MatchState::OrangeWin:
            result = "Winner: ORANGE";
            resultColor = orangeColor;
            break;
        case MatchState::BlueWin:
            result = "Winner: BLUE";
            resultColor = blueColor;
            break;
        case MatchState::Draw:
            result = "Match ended in a draw.";
            break;
        default:
            break;
        }
        DrawString(2.0, baseY - 14.0, result, resultColor);
        std::ostringstream osTime;
        osTime << "Duration: " << std::fixed << std::setprecision(1) << matchEndTime << "s";
        DrawString(2.0, baseY - 18.0, osTime.str());
        DrawString(2.0, baseY - 22.0, "Press R to restart simulation.");
    }

    DrawString(120.0, baseY, "Controls: S/V overlays, Right-Click=Security, 1/2 teams, 0 off, R reset");

    glPopMatrix();
}

static void CheckWinCondition()
{
    if (matchState != MatchState::Running) return;

    bool orangeAlive = CountAlive(teamOrange) > 0;
    bool blueAlive = CountAlive(teamBlue) > 0;

    if (orangeAlive && blueAlive) return;

    double now = clock() / (double)CLOCKS_PER_SEC;
    matchEndTime = now - matchStartTime;

    if (!orangeAlive && !blueAlive) {
        matchState = MatchState::Draw;
    }
    else if (orangeAlive) {
        matchState = MatchState::OrangeWin;
    }
    else {
        matchState = MatchState::BlueWin;
    }

    std::string winMsg;
    switch (matchState) {
    case MatchState::OrangeWin: winMsg = "Orange Team Wins!"; break;
    case MatchState::BlueWin:   winMsg = "Blue Team Wins!";   break;
    case MatchState::Draw:      winMsg = "Match ended in a draw."; break;
    default: break;
    }
    if (!winMsg.empty()) {
#ifdef _WIN32
        MessageBoxA(nullptr, winMsg.c_str(), "Battle Result", MB_OK | MB_TOPMOST);
#endif
    }
}

// ================== Drawing ==================
static bool FindLocalPatrolTarget(NPC* npc, int radius, int& outX, int& outY)
{
    if (!npc) return false;
    int baseX = static_cast<int>(npc->getX() + 0.5);
    int baseY = static_cast<int>(npc->getY() + 0.5);
    const int maxAttempts = 18;

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        int dx = (rand() % (radius * 2 + 1)) - radius;
        int dy = (rand() % (radius * 2 + 1)) - radius;
        int candidateX = baseX + dx;
        int candidateY = baseY + dy;
        if (dx == 0 && dy == 0) continue;
        if (!Map::InBounds(candidateX, candidateY)) continue;
        if (!Map::IsWalkable(candidateX, candidateY)) continue;
        if (Map::IsOccupied(candidateX, candidateY, npc->GetId())) continue;

        double dist2 = (candidateX - npc->getX()) * (candidateX - npc->getX()) +
            (candidateY - npc->getY()) * (candidateY - npc->getY());
        if (dist2 < 9.0) continue; // skip targets too close

        double risk = Map::GetSecurityValue(candidateY, candidateX, npc->getTeam());
        if (risk > 0.78) continue;

        outX = candidateX;
        outY = candidateY;
        return true;
    }
    return false;
}

static void EnsureIdleMotion(NPC* npc, double currentTime)
{
    if (!npc || !npc->IsAlive()) return;
    if (npc->getIsMoving() || npc->getCurrentState()) return;

    Role role = npc->getRole();
    if (role == Role::Commander) return;

    if ((currentTime - npc->GetLastIdleAnchorTime()) < 3.0) return;

    int targetX = 0, targetY = 0;
    if (FindLocalPatrolTarget(npc, 9, targetX, targetY)) {
        npc->setOrderTarget(targetX, targetY);
        npc->MarkIdleAnchorIssued();
        npc->setCurrentState(new GoToCover());
        npc->getCurrentState()->OnEnter(npc);
        npc->clearOrderTarget();
        return;
    }

    int startX = static_cast<int>(std::round(npc->getX()));
    int startY = static_cast<int>(std::round(npc->getY()));

    std::pair<int, int> safeCell;
    if (Path::FindNearestCover(startX, startY, 12, npc->getTeam(), safeCell)) {
        npc->setOrderTarget(safeCell.first, safeCell.second);
        npc->MarkIdleAnchorIssued();
        npc->GoToGrid(safeCell.first, safeCell.second);
        npc->clearOrderTarget();
        return;
    }

    int fallbackX = startX + ((rand() % 11) - 5);
    int fallbackY = startY + ((rand() % 11) - 5);
    if (Map::InBounds(fallbackX, fallbackY) && Map::IsWalkable(fallbackX, fallbackY)) {
        npc->setOrderTarget(fallbackX, fallbackY);
        npc->MarkIdleAnchorIssued();
        npc->GoToGrid(fallbackX, fallbackY);
        npc->clearOrderTarget();
    }
}

void DrawTree(double x, double y, double size)
{
    glColor3d(0.09, 0.45, 0.10);
    glBegin(GL_POLYGON);
    glVertex2d(x, y + size);
    glVertex2d(x - 0.7 * size, y - 0.5 * size);
    glVertex2d(x + 0.7 * size, y - 0.5 * size);
    glEnd();
    glColor3d(0, 0.25, 0.05);
    glBegin(GL_LINE_LOOP);
    glVertex2d(x, y + size);
    glVertex2d(x - 0.7 * size, y - 0.5 * size);
    glVertex2d(x + 0.7 * size, y - 0.5 * size);
    glEnd();
}

void DrawWarehouse(double x, double y, double size)
{
    glColor3d(1.0, 1.0, 0.0);
    glBegin(GL_POLYGON);
    glVertex2d(x - size / 2, y - size / 2);
    glVertex2d(x + size / 2, y - size / 2);
    glVertex2d(x + size / 2, y + size / 2);
    glVertex2d(x - size / 2, y + size / 2);
    glEnd();
    glColor3d(0, 0, 0);
    glBegin(GL_LINE_LOOP);
    glVertex2d(x - size / 2, y - size / 2);
    glVertex2d(x + size / 2, y - size / 2);
    glVertex2d(x + size / 2, y + size / 2);
    glVertex2d(x - size / 2, y + size / 2);
    glEnd();
}

void DrawRock(double x, double y, double size)
{
    glColor3d(0.55, 0.55, 0.55);
    glBegin(GL_POLYGON);
    glVertex2d(x - size / 2, y - size / 2);
    glVertex2d(x + size / 2, y - size / 2);
    glVertex2d(x + size / 2, y + size / 2);
    glVertex2d(x - size / 2, y + size / 2);
    glEnd();
    glColor3d(0, 0, 0);
    glBegin(GL_LINE_LOOP);
    glVertex2d(x - size / 2, y - size / 2);
    glVertex2d(x + size / 2, y - size / 2);
    glVertex2d(x + size / 2, y + size / 2);
    glVertex2d(x - size / 2, y + size / 2);
    glEnd();
}

void DrawWaterPuddle(double cx, double cy, double rx, double ry)
{
    glColor3d(0.72, 0.84, 0.95);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2d(cx, cy);
    const int N = 64;
    for (int i = 0; i <= N; ++i) {
        double t = (2.0 * M_PI * i) / N;
        double wobble = 0.08 * sin(3.0 * t) + 0.05 * cos(5.0 * t);
        double R = 1.0 + wobble;
        glVertex2d(cx + R * rx * cos(t), cy + R * ry * sin(t));
    }
    glEnd();
    glColor3d(0, 0, 0);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < N; ++i) {
        double t = (2.0 * M_PI * i) / N;
        double wobble = 0.08 * sin(3.0 * t) + 0.05 * cos(5.0 * t);
        double R = 1.0 + wobble;
        glVertex2d(cx + R * rx * cos(t), cy + R * ry * sin(t));
    }
    glEnd();
}

void DrawField()
{
    // Trees
    DrawTree(70, 73, 3.5);
    DrawTree(76, 78, 3.5);
    DrawTree(156, 46, 3.5);
    DrawTree(76, 38, 3.5);
    DrawTree(86, 42, 3.5);
    DrawTree(92, 36, 3.5);
    DrawTree(44, 48, 3.5);
    DrawTree(48, 44, 3.5);
    DrawTree(140, 28, 3.5);
    DrawTree(148, 26, 3.5);
    DrawTree(164, 70, 3.5);
    DrawTree(170, 74, 3.5);
    DrawTree(124, 80, 3.0);  
    DrawTree(118, 74, 3.2);
    DrawTree(112, 68, 3.0);

    // Warehouses
    DrawWarehouse(20, 85, 5);
    DrawWarehouse(15, 20, 5);
    DrawWarehouse(180, 85, 5);
    DrawWarehouse(185, 20, 5);

    // Rocks
    DrawRock(45, 65, 5);
    DrawRock(51, 60, 5);
    DrawRock(132, 50, 5);
    DrawRock(132, 45, 5);
    DrawRock(32, 37, 5);
    DrawRock(150, 64, 5);
    DrawRock(102, 40, 5);
    DrawRock(118, 32, 5);
    DrawRock(172, 48, 5);

    // Water
    DrawWaterPuddle(98, 65, 10, 5);
    DrawWaterPuddle(55, 36, 8, 5);
}

void DrawDebugMap()
{
    for (int y = 0; y < Map::H; y++)
    {
        for (int x = 0; x < Map::W; x++)
        {
            auto c = Map::Get(x, y);
            if (c == Map::ROCK) glColor3d(0.4, 0.4, 0.4);
            else if (c == Map::WATER) glColor3d(0.3, 0.6, 0.9);
            else if (c == Map::WAREHOUSE) glColor3d(1.0, 1.0, 0.0);
            else if (c == Map::TREE) glColor3d(0.0, 0.5, 0.0);
            else continue;
            glBegin(GL_POINTS);
            glVertex2d(x, y);
            glEnd();
        }
    }
}

// ================== Simulation ==================
static void DrawAllAgents()
{
    for (NPC* a : teamOrange) if (a) a->Show();
    for (NPC* a : teamBlue)   if (a) a->Show();
    
    // Draw active grenades
    for (Grenade* g : activeGrenades) {
        if (g && g->GetIsExploding()) {
            g->Show();
        }
    }

    DrawActiveGunshots();
}

static void UpdateAllAgents(double currentTime)
{
    for (NPC* a : teamOrange)
    {
        if (!a || !a->IsAlive()) continue;  // Dead NPCs don't update
        a->DoSomeWork();
        if (a->getCurrentState()) a->getCurrentState()->Transition(a);
    }
    for (NPC* a : teamBlue)
    {
        if (!a || !a->IsAlive()) continue;  // Dead NPCs don't update
        a->DoSomeWork();
        if (a->getCurrentState()) a->getCurrentState()->Transition(a);
    }

    UpdateActiveGunshots();
    
    // Update active grenades
    for (auto it = activeGrenades.begin(); it != activeGrenades.end();) {
        Grenade* g = *it;
        if (g && g->GetIsExploding()) {
            g->Explode();
            ++it;
        } else {
            delete g;
            it = activeGrenades.erase(it);
        }
    }

    for (NPC* a : teamOrange) {
        EnsureIdleMotion(a, currentTime);
    }
    for (NPC* a : teamBlue) {
        EnsureIdleMotion(a, currentTime);
    }

    Map::DecayDynamicCosts(0.96);
}

// ================== GLUT callbacks ==================
void display()
{
    glClear(GL_COLOR_BUFFER_BIT);

    if (g_showVisibility) {
        Map::DrawVisibilityMap();
    }
    else {
        DrawField();
    }

    if (g_showSecurity) {
        Map::DrawSecurityMap(g_securityOverlayTeam);
    }

    DrawAllAgents();
    DrawHud();

    DrawDebugMap();


    glutSwapBuffers();
}

const double COMMANDER_UPDATE_INTERVAL = 1.0; // Commander gives new orders every second

void idle()
{
    double currentTime = clock() / (double)CLOCKS_PER_SEC;

    if (matchState == MatchState::Running) {
        UpdateAllAgents(currentTime);

        RebuildSecurityMap();

        Map::UpdateVisibilityMap(teamOrange);
        Map::UpdateVisibilityMap(teamBlue);

        if (currentTime - lastCommanderUpdateTime > COMMANDER_UPDATE_INTERVAL) {
            if (commanderOrange && teamOrange.size() > 0 && teamOrange[0] && teamOrange[0]->IsAlive()) {
                commanderOrange->PlanAndAssignOrders();
            }
            if (commanderBlue && teamBlue.size() > 0 && teamBlue[0] && teamBlue[0]->IsAlive()) {
                commanderBlue->PlanAndAssignOrders();
            }
            lastCommanderUpdateTime = currentTime;
        }

        CheckWinCondition();
    }

    glutPostRedisplay();
}

void OnKeyboard(unsigned char key, int, int)
{
    const unsigned char lower = static_cast<unsigned char>(std::tolower(key));
    switch (lower) {
    case 'v':
        g_showVisibility = !g_showVisibility;
        break;
    case 's':
        g_showSecurity = !g_showSecurity;
        break;
    case 'r':
        ResetSimulation();
        return;
    case '0':
        g_showSecurity = false;
        break;
    case '1':
        g_securityOverlayTeam = TeamId::Orange;
        g_showSecurity = true;
        break;
    case '2':
        g_securityOverlayTeam = TeamId::Blue;
        g_showSecurity = true;
        break;
    default:
        break;
    }
    glutPostRedisplay();
}

void OnMouse(int button, int state, int, int)
{
    if (button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN) {
        g_showSecurity = !g_showSecurity;
        glutPostRedisplay();
    }
}

void main(int argc, char* argv[])
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    glutInitWindowSize(900, 450);
    glutInitWindowPosition(400, 100);
    glutCreateWindow("AI Battle Simulation");

    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutKeyboardFunc(OnKeyboard);
    glutMouseFunc(OnMouse);
    init();
    glutMainLoop();
}
