#pragma once
// ============================================================================
// RTSurvivor — симуляция игры (чистая логика, без привязки к raylib).
// Можно гонять headless: тесты, балансировка, боты.
// ============================================================================
#include <vector>
#include <cmath>

struct Vec2 {
    float x = 0.f, y = 0.f;
    Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
    Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
    Vec2 operator*(float s) const { return { x * s, y * s }; }
};

inline float len(Vec2 v) { return sqrtf(v.x * v.x + v.y * v.y); }
inline Vec2 norm(Vec2 v) { float l = len(v); return l > 0.0001f ? v * (1.f / l) : Vec2{ 0, 0 }; }
inline float dist(Vec2 a, Vec2 b) { return len(a - b); }

// --- Перечисления -----------------------------------------------------------
enum class BType { TownCenter, House, Farm, Sawmill, Mine, Barracks, Tower, Count };
enum class UType { Swordsman, Archer, Crossbowman, Count };
enum class EType { Runner, Brute, Spitter, Count };
enum class Phase { Day, Night, Boost, GameOver };

// --- Сущности ---------------------------------------------------------------
struct Building {
    BType type;
    Vec2 pos;              // центр, мировые координаты
    float hp, maxHp;
    float cooldown = 0.f;  // перезарядка атаки (башни)
    bool alive = true;
};

struct Unit {
    UType type;
    Vec2 pos;
    float hp, maxHp;
    float cooldown = 0.f;
    int target = -1;       // индекс во врагах, -1 = нет цели
    Vec2 anchor;           // точка сбора рядом с ратушей
    bool alive = true;
};

struct Enemy {
    EType type;
    Vec2 pos;
    float hp, maxHp;
    float dmg;             // уже с учётом скалирования волны
    float cooldown = 0.f;
    int target = -1;       // индекс цели
    bool targetIsUnit = false;
    bool alive = true;
};

// --- Игровое состояние ------------------------------------------------------
struct Game {
    // Ресурсы
    float wood = 120.f, food = 90.f, gold = 60.f;
    int pop = 0, popCap = 0;

    // Фазы и волны
    Phase phase = Phase::Day;
    float phaseTimer = 35.f;     // длительность дня, сек
    int wave = 0;
    bool nightActive = false;

    // Модификаторы бустов
    float unitDmgMult = 1.f, unitHpMult = 1.f;
    float woodMult = 1.f, foodMult = 1.f, goldMult = 1.f;
    float towerDmgMult = 1.f, unitCostMult = 1.f;
    float regen = 0.f;
    bool crossbowUnlocked = false;

    // Мир
    std::vector<Building> buildings;
    std::vector<Unit> units;
    std::vector<Enemy> enemies;
    int enemiesKilled = 0;
    int offer[3] = { -1, -1, -1 };   // предлагаемые бусты (id), -1 = нет

    // API для UI и тестов
    void reset();
    void tick(float dt);

    bool canAfford(BType t) const;
    bool canPlace(BType t, int gx, int gy) const;   // gx,gy — клетка сетки (48px)
    bool place(BType t, int gx, int gy);            // списывает ресурсы
    bool hire(UType t);                             // нанять юнита (нужна казарма)

    int boostCount() const { return 10; }
    const char* boostName(int id) const;
    const char* boostDesc(int id) const;
    void applyBoost(int slot);                      // 0..2, переводит игру в Day

    // Данные для отладки/рендера
    static constexpr float CELL = 48.f;
    static constexpr float MAP_PX = 48.f * 48.f;    // 2304 x 2304
    static constexpr float BSIZE = 96.f;            // здание 2x2 клетки
};

// Таблицы статов (определены в sim.cpp, доступны рендеру)
const char* bName(BType t);
float bCostWood(BType t);
float bCostFood(BType t);
float bCostGold(BType t);
const char* uName(UType t);
float uCostFood(UType t);
float uCostGold(UType t);
const char* eName(EType t);
