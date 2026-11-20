#include "sim.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

// ============================================================================
// Таблицы статов и стоимости
// ============================================================================
static const char* B_NAMES[]  = { "Town Center", "House", "Farm", "Sawmill", "Mine", "Barracks", "Tower" };
static const float B_HP[]     = { 600.f, 120.f, 80.f, 100.f, 100.f, 250.f, 200.f };
static const float B_WOOD[]   = { 0, 25, 35, 50, 60, 100, 60 };
static const float B_FOOD[]   = { 0, 0, 0, 0, 0, 0, 0 };
static const float B_GOLD[]   = { 0, 0, 0, 0, 10, 40, 25 };
// доход в секунду: { wood, food, gold }
static const float B_INC[][3] = { {0,1.2f,0}, {0,0,0}, {0,2.f,0}, {2.5f,0,0}, {0,0,1.8f}, {0,0,0}, {0,0,0} };
static const int   B_POP[]    = { 6, 4, 0, 0, 0, 0, 0 };

const char* bName(BType t) { return B_NAMES[(int)t]; }
float bCostWood(BType t) { return B_WOOD[(int)t]; }
float bCostFood(BType t) { return B_FOOD[(int)t]; }
float bCostGold(BType t) { return B_GOLD[(int)t]; }

static const char* U_NAMES[] = { "Swordsman", "Archer", "Crossbowman" };
static const float U_HP[]    = { 65.f, 42.f, 55.f };
static const float U_DMG[]   = { 9.f, 11.f, 26.f };
static const float U_RANGE[] = { 26.f, 170.f, 210.f };
static const float U_SPEED[] = { 65.f, 60.f, 55.f };
static const float U_CD[]    = { 0.8f, 1.1f, 2.2f };
static const float U_RAD[]   = { 10.f, 10.f, 10.f };
static const float U_FOOD[]  = { 20.f, 15.f, 25.f };
static const float U_GOLD[]  = { 10.f, 20.f, 30.f };

const char* uName(UType t) { return U_NAMES[(int)t]; }
float uCostFood(UType t) { return U_FOOD[(int)t]; }
float uCostGold(UType t) { return U_GOLD[(int)t]; }

static const char* E_NAMES[] = { "Runner", "Brute", "Spitter" };
static const float E_HP[]    = { 30.f, 130.f, 45.f };
static const float E_DMG[]   = { 6.f, 16.f, 9.f };
static const float E_RANGE[] = { 22.f, 28.f, 150.f };
static const float E_SPEED[] = { 75.f, 42.f, 55.f };
static const float E_CD[]    = { 0.9f, 1.4f, 1.6f };
static const float E_RAD[]   = { 11.f, 16.f, 12.f };

const char* eName(EType t) { return E_NAMES[(int)t]; }

// Башня
static const float TOWER_RANGE = 230.f;
static const float TOWER_DMG = 10.f;
static const float TOWER_CD = 0.9f;

static const float DAY_LEN = 35.f;

// ============================================================================
// Пространственная сетка для быстрого поиска соседей (O(1) в среднем)
// ============================================================================
namespace {
constexpr int GCELLS = 48;
struct Grid {
    std::vector<int> cells[GCELLS * GCELLS]; // индексы: юниты с 0, враги со смещением
    void clear() { for (auto& c : cells) c.clear(); }
    void insert(float x, float y, int idx) {
        int cx = (int)(x / Game::CELL), cy = (int)(y / Game::CELL);
        if (cx < 0) cx = 0; if (cx >= GCELLS) cx = GCELLS - 1;
        if (cy < 0) cy = 0; if (cy >= GCELLS) cy = GCELLS - 1;
        cells[cy * GCELLS + cx].push_back(idx);
    }
};
}

// ============================================================================
// Game
// ============================================================================
void Game::reset() {
    *this = Game();
    // Стартовая ратуша в центре карты
    Building tc;
    tc.type = BType::TownCenter;
    tc.pos = { MAP_PX / 2, MAP_PX / 2 };
    tc.maxHp = tc.hp = B_HP[(int)BType::TownCenter];
    buildings.push_back(tc);
}

bool Game::canAfford(BType t) const {
    return wood >= bCostWood(t) && food >= bCostFood(t) && gold >= bCostGold(t);
}

bool Game::canPlace(BType t, int gx, int gy) const {
    (void)t;
    if (gx < 0 || gy < 0 || gx >= GCELLS - 1 || gy >= GCELLS - 1) return false;
    Vec2 p = { gx * CELL + BSIZE / 2, gy * CELL + BSIZE / 2 };
    for (const auto& b : buildings) {
        if (!b.alive) continue;
        if (fabsf(b.pos.x - p.x) < BSIZE && fabsf(b.pos.y - p.y) < BSIZE) return false;
    }
    return true;
}

bool Game::place(BType t, int gx, int gy) {
    if (!canAfford(t) || !canPlace(t, gx, gy)) return false;
    wood -= bCostWood(t); food -= bCostFood(t); gold -= bCostGold(t);
    Building b;
    b.type = t;
    b.pos = { gx * CELL + BSIZE / 2, gy * CELL + BSIZE / 2 };
    b.maxHp = b.hp = B_HP[(int)t];
    buildings.push_back(b);
    return true;
}

bool Game::hire(UType t) {
    bool hasBarracks = false;
    for (const auto& b : buildings) if (b.alive && b.type == BType::Barracks) hasBarracks = true;
    if (!hasBarracks) return false;
    if (t == UType::Crossbowman && !crossbowUnlocked) return false;
    int cap = 0;
    for (const auto& b : buildings)
        if (b.alive) cap += B_POP[(int)b.type];
    float cf = uCostFood(t) * unitCostMult, cg = uCostGold(t) * unitCostMult;
    if (food < cf || gold < cg || (int)units.size() >= cap) return false;
    food -= cf; gold -= cg;
    // спавн возле случайной казармы
    Vec2 spawn = { MAP_PX / 2, MAP_PX / 2 };
    for (const auto& b : buildings)
        if (b.alive && b.type == BType::Barracks) { spawn = b.pos; break; }
    Unit u;
    u.type = t;
    u.pos = spawn + Vec2{ (float)(rand() % 80 - 40), 70.f + (float)(rand() % 40) };
    u.maxHp = u.hp = U_HP[(int)t] * unitHpMult;
    u.anchor = u.pos;
    units.push_back(u);
    return true;
}

// --- Бусты ------------------------------------------------------------------
static const char* BOOST_NAMES[] = {
    "Tower Masters", "Sharp Blades", "Sturdy Armor", "Lumber Industry", "Rich Harvest",
    "Gold Rush", "War Economy", "Crossbow License", "Field Medics", "Spoils of War" };
static const char* BOOST_DESCS[] = {
    "Towers deal +35% damage", "Units deal +20% damage", "Units get +25% max HP and heal 25%",
    "+30% wood income", "+30% food income", "+30% gold income",
    "Units cost 20% less", "Unlock Crossbowman at the Barracks",
    "Units regenerate +2 HP/s", "Instantly gain 120 wood and 60 gold" };

const char* Game::boostName(int id) const { return BOOST_NAMES[id]; }
const char* Game::boostDesc(int id) const { return BOOST_DESCS[id]; }

void Game::applyBoost(int slot) {
    if (phase != Phase::Boost || slot < 0 || slot > 2) return;
    switch (offer[slot]) {
    case 0: towerDmgMult += 0.35f; break;
    case 1: unitDmgMult += 0.20f; break;
    case 2:
        unitHpMult *= 1.25f;
        for (auto& u : units) { u.maxHp *= 1.25f; u.hp = std::min(u.maxHp, u.hp + u.maxHp * 0.25f); }
        break;
    case 3: woodMult += 0.30f; break;
    case 4: foodMult += 0.30f; break;
    case 5: goldMult += 0.30f; break;
    case 6: unitCostMult *= 0.80f; break;
    case 7: crossbowUnlocked = true; break;
    case 8: regen += 2.f; break;
    case 9: wood += 120.f; gold += 60.f; break;
    default: break;
    }
    phase = Phase::Day;
    phaseTimer = DAY_LEN;
    offer[0] = offer[1] = offer[2] = -1;
}

static void rollOffers(Game& g) {
    bool used[10] = {};
    for (int i = 0; i < 3; ) {
        int id = rand() % 10;
        if (used[id]) continue;
        used[id] = true;
        g.offer[i++] = id;
    }
}

// --- Волны ------------------------------------------------------------------
static void startNight(Game& g) {
    g.wave++;
    g.phase = Phase::Night;
    g.nightActive = true;
    int count = 3 + g.wave * 3;
    float hpScale = 1.f + 0.10f * (g.wave - 1);
    float dmgScale = 1.f + 0.05f * (g.wave - 1);
    int brutes = g.wave >= 3 ? count / 4 : 0;
    int spitters = g.wave >= 5 ? count / 5 : 0;
    int runners = count - brutes - spitters;
    auto spawnOne = [&](EType t) {
        // случайная точка на краю карты
        int side = rand() % 4;
        Vec2 p;
        if (side == 0) p = { (float)(rand() % (int)Game::MAP_PX), 0.f };
        else if (side == 1) p = { (float)(rand() % (int)Game::MAP_PX), Game::MAP_PX };
        else if (side == 2) p = { 0.f, (float)(rand() % (int)Game::MAP_PX) };
        else p = { Game::MAP_PX, (float)(rand() % (int)Game::MAP_PX) };
        Enemy e;
        e.type = t;
        e.pos = p;
        e.maxHp = e.hp = E_HP[(int)t] * hpScale;
        e.dmg = E_DMG[(int)t] * dmgScale;
        g.enemies.push_back(e);
    };
    for (int i = 0; i < runners; i++) spawnOne(EType::Runner);
    for (int i = 0; i < brutes; i++) spawnOne(EType::Brute);
    for (int i = 0; i < spitters; i++) spawnOne(EType::Spitter);
}

// --- Поиск целей через сетку -------------------------------------------------
static int nearestEnemy(Game& g, Grid& grid, Vec2 from, float maxDist) {
    int best = -1; float bestD = maxDist;
    int cx = (int)(from.x / Game::CELL), cy = (int)(from.y / Game::CELL);
    int r = (int)(maxDist / Game::CELL) + 1;
    for (int gy = cy - r; gy <= cy + r; gy++) {
        if (gy < 0 || gy >= GCELLS) continue;
        for (int gx = cx - r; gx <= cx + r; gx++) {
            if (gx < 0 || gx >= GCELLS) continue;
            for (int idx : grid.cells[gy * GCELLS + gx]) {
                if (idx < (int)g.units.size()) continue; // это юнит
                int e = idx - (int)g.units.size();
                float d = dist(from, g.enemies[e].pos);
                if (d < bestD) { bestD = d; best = e; }
            }
        }
    }
    return best;
}

// ближайшая цель для врага: юнит или здание
static void nearestVictim(Game& g, Grid& grid, Vec2 from, int& outIdx, bool& outIsUnit) {
    float bestD = 1e9f; outIdx = -1;
    int cx = (int)(from.x / Game::CELL), cy = (int)(from.y / Game::CELL);
    int r = GCELLS; // враги ищут глобально
    for (int gy = cy - r; gy <= cy + r; gy++) {
        if (gy < 0 || gy >= GCELLS) continue;
        for (int gx = cx - r; gx <= cx + r; gx++) {
            if (gx < 0 || gx >= GCELLS) continue;
            for (int idx : grid.cells[gy * GCELLS + gx]) {
                if (idx < (int)g.units.size()) {
                    int u = idx;
                    float d = dist(from, g.units[u].pos);
                    if (d < bestD) { bestD = d; outIdx = u; outIsUnit = true; }
                }
            }
        }
    }
    for (size_t i = 0; i < g.buildings.size(); i++) {
        if (!g.buildings[i].alive) continue;
        float d = dist(from, g.buildings[i].pos);
        if (d < bestD) { bestD = d; outIdx = (int)i; outIsUnit = false; }
    }
}

// --- Главный тик --------------------------------------------------------------
void Game::tick(float dt) {
    if (phase == Phase::Boost || phase == Phase::GameOver) return;

    // Фазовый таймер
    phaseTimer -= dt;
    if (phase == Phase::Day && phaseTimer <= 0.f) startNight(*this);

    // Пересчёт лимита населения
    popCap = 0;
    for (const auto& b : buildings)
        if (b.alive) popCap += B_POP[(int)b.type];
    pop = (int)units.size();

    // Экономика
    for (const auto& b : buildings) {
        if (!b.alive) continue;
        wood += B_INC[(int)b.type][0] * woodMult * dt;
        food += B_INC[(int)b.type][1] * foodMult * dt;
        gold += B_INC[(int)b.type][2] * goldMult * dt;
    }

    // Регенерация юнитов (буст)
    if (regen > 0.f)
        for (auto& u : units) u.hp = std::min(u.maxHp, u.hp + regen * dt);

    // Сетка по юнитам и врагам
    Grid grid;
    for (int i = 0; i < (int)units.size(); i++) grid.insert(units[i].pos.x, units[i].pos.y, i);
    for (int i = 0; i < (int)enemies.size(); i++) grid.insert(enemies[i].pos.x, enemies[i].pos.y, (int)units.size() + i);

    // --- Юниты игрока ---
    for (auto& u : units) {
        u.cooldown -= dt;
        // валидация/обновление цели
        if (u.target < 0 || u.target >= (int)enemies.size() || !enemies[u.target].alive)
            u.target = nearestEnemy(*this, grid, u.pos, 900.f);
        if (u.target >= 0) {
            auto& e = enemies[u.target];
            float reach = U_RANGE[(int)u.type] + E_RAD[(int)e.type];
            float d = dist(u.pos, e.pos);
            if (d <= reach) {
                if (u.cooldown <= 0.f) {
                    e.hp -= U_DMG[(int)u.type] * unitDmgMult;
                    u.cooldown = U_CD[(int)u.type];
                }
            } else {
                u.pos = u.pos + norm(e.pos - u.pos) * U_SPEED[(int)u.type] * dt;
            }
        } else {
            // возвращаемся к точке сбора
            float d = dist(u.pos, u.anchor);
            if (d > 12.f) u.pos = u.pos + norm(u.anchor - u.pos) * U_SPEED[(int)u.type] * dt;
        }
    }

    // --- Башни ---
    for (auto& b : buildings) {
        if (!b.alive || b.type != BType::Tower) continue;
        b.cooldown -= dt;
        if (b.cooldown > 0.f) continue;
        int t = nearestEnemy(*this, grid, b.pos, TOWER_RANGE);
        if (t >= 0) {
            enemies[t].hp -= TOWER_DMG * towerDmgMult;
            b.cooldown = TOWER_CD;
        }
    }

    // --- Враги ---
    for (auto& e : enemies) {
        e.cooldown -= dt;
        // перенацеливание
        if (e.target < 0 || (e.targetIsUnit && (e.target >= (int)units.size() || !units[e.target].alive)) ||
            (!e.targetIsUnit && (e.target >= (int)buildings.size() || !buildings[e.target].alive))) {
            nearestVictim(*this, grid, e.pos, e.target, e.targetIsUnit);
        }
        if (e.target < 0) continue; // целей нет (не должно случаться: ратуша есть)
        Vec2 tp = e.targetIsUnit ? units[e.target].pos : buildings[e.target].pos;
        float reach = E_RANGE[(int)e.type] + (e.targetIsUnit ? U_RAD[(int)units[e.target].type] : BSIZE / 2);
        float d = dist(e.pos, tp);
        if (d <= reach) {
            if (e.cooldown <= 0.f) {
                if (e.targetIsUnit) units[e.target].hp -= e.dmg;
                else {
                    buildings[e.target].hp -= e.dmg;
                    if (buildings[e.target].hp <= 0.f) {
                        buildings[e.target].alive = false;
                        if (buildings[e.target].type == BType::TownCenter) phase = Phase::GameOver;
                    }
                }
                e.cooldown = E_CD[(int)e.type];
            }
        } else {
            e.pos = e.pos + norm(tp - e.pos) * E_SPEED[(int)e.type] * dt;
        }
    }

    // --- Уборка погибших ---
    int before = (int)enemies.size();
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
        [](const Enemy& e) { return e.hp <= 0.f; }), enemies.end());
    enemiesKilled += before - (int)enemies.size();
    units.erase(std::remove_if(units.begin(), units.end(),
        [](const Unit& u) { return u.hp <= 0.f; }), units.end());

    // --- Конец ночи ---
    if (phase == Phase::Night && nightActive && enemies.empty()) {
        phase = Phase::Boost;
        nightActive = false;
        rollOffers(*this);
    }
}
