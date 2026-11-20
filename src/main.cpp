#include "raylib.h"
#include "sim.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

// ============================================================================
// RTSurvivor — MVP: окно, рендер, UI. Симуляция — в sim.cpp (headless-готовая).
// ============================================================================

static Font gFont;
static bool gFontLoaded = false;

// Кириллица для UI: ASCII 32..126 + кириллица U+0400..U+045F
static int gCps[95 + 96];

static void txt(const char* s, float x, float y, float size, Color c) {
    if (gFontLoaded) DrawTextEx(gFont, s, { x, y }, size, 1.f, c);
    else DrawText(s, (int)x, (int)y, (int)size, c);
}
static float txtW(const char* s, float size) {
    return gFontLoaded ? MeasureTextEx(gFont, s, size, 1.f).x : (float)MeasureText(s, (int)size);
}
static void txtC(const char* s, float cx, float y, float size, Color c) {
    txt(s, cx - txtW(s, size) / 2.f, y, size, c);
}

// --- Палитра и цвета сущностей ----------------------------------------------
static Color BColors[] = {
    { 190, 150, 90, 255 },   // TownCenter
    { 160, 120, 80, 255 },   // House
    { 120, 170, 70, 255 },   // Farm
    { 140, 100, 60, 255 },   // Sawmill
    { 120, 120, 130, 255 },  // Mine
    { 150, 80, 80, 255 },    // Barracks
    { 90, 90, 110, 255 },    // Tower
};
static Color UColors[] = { { 70, 130, 220, 255 }, { 60, 190, 190, 255 }, { 170, 110, 230, 255 } };
static Color EColors[] = { { 220, 120, 60, 255 }, { 180, 50, 50, 255 }, { 220, 70, 160, 255 } };

static void hpBar(float x, float y, float w, float hp, float maxHp) {
    float f = hp / maxHp;
    if (f >= 0.999f) return;
    DrawRectangle((int)x, (int)y, (int)w, 4, { 40, 40, 40, 200 });
    DrawRectangle((int)x, (int)y, (int)(w * f), 4, f > 0.4f ? GREEN : RED);
}

// --- Headless-самотест симуляции ---------------------------------------------
static int selftest() {
    srand(12345);
    Game g; g.reset();
    g.wood = g.food = g.gold = 5000;

    BType plan[] = { BType::House, BType::House, BType::Farm, BType::Farm, BType::Sawmill,
                     BType::Mine, BType::Barracks, BType::Tower, BType::Tower };
    int px[] = { 22, 26, 22, 26, 29, 29, 31, 24, 28 };
    int py[] = { 20, 20, 25, 24, 20, 24, 22, 28, 28 };
    int placed = 0;
    for (int i = 0; i < 9; i++) if (g.place(plan[i], px[i], py[i])) placed++;

    int h = 0;
    while (h < 4 && g.hire(UType::Swordsman)) h++;
    while (h < 8 && g.hire(UType::Archer)) h++;

    int ticks = 0, boosts = 0;
    while (g.phase != Phase::GameOver && ticks < 60 * 300 && g.wave < 3) {
        g.tick(1.f / 60.f);
        ticks++;
        if (g.phase == Phase::Boost) { g.applyBoost(0); boosts++; }
    }

    int bAlive = 0;
    for (const auto& b : g.buildings) if (b.alive) bAlive++;

    printf("=== SELFTEST ===\n");
    printf("buildings placed: %d/9, hired: %d\n", placed, h);
    printf("wave reached: %d, boosts applied: %d, kills: %d\n", g.wave, boosts, g.enemiesKilled);
    printf("units alive: %zu, buildings alive: %d, wood: %.0f food: %.0f gold: %.0f\n",
        g.units.size(), bAlive, g.wood, g.food, g.gold);
    bool ok = placed >= 8 && h >= 6 && boosts >= 1;
    printf(ok ? "SELFTEST: PASS\n" : "SELFTEST: FAIL\n");
    return ok ? 0 : 1;
}

// --- main ---------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) return selftest();

    srand((unsigned)time(nullptr));

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "RTSurvivor — RTS x Vampire Survivors (MVP)");
    SetTargetFPS(60);

    // Шрифт с кириллицей
    for (int i = 0; i < 95; i++) gCps[i] = 32 + i;
    for (int i = 0; i < 96; i++) gCps[95 + i] = 0x400 + i;
    gFont = LoadFontEx("assets/fonts/ptsans.ttf", 36, gCps, 95 + 96);
    gFontLoaded = gFont.texture.id != 0;

    Game game; game.reset();

    Camera2D cam = {};
    cam.target = { Game::MAP_PX / 2, Game::MAP_PX / 2 };
    cam.offset = { (float)GetScreenWidth() / 2, (float)GetScreenHeight() / 2 };
    cam.zoom = 1.f;

    int buildSel = -1;              // выбранное здание для постройки, -1 = нет
    const float DT = 1.f / 60.f;    // фиксированный шаг симуляции
    float acc = 0.f;

    while (!WindowShouldClose()) {
        // --- Ввод: камера ---
        float pan = 700.f * GetFrameTime() / cam.zoom;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) cam.target.y -= pan;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) cam.target.y += pan;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) cam.target.x -= pan;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) cam.target.x += pan;
        float wheel = GetMouseWheelMove();
        if (wheel != 0.f) {
            Vector2 mw = GetScreenToWorld2D(GetMousePosition(), cam);
            float z = cam.zoom * (1.f + wheel * 0.12f);
            cam.zoom = z < 0.45f ? 0.45f : (z > 2.5f ? 2.5f : z);
            cam.offset = GetMousePosition();
            cam.target = mw;
        }
        cam.offset = { (float)GetScreenWidth() / 2, (float)GetScreenHeight() / 2 };

        Vector2 mScreen = GetMousePosition();
        Vec2 mw = { 0, 0 };
        { Vector2 v = GetScreenToWorld2D(mScreen, cam); mw = { v.x, v.y }; }
        int mgx = (int)(mw.x / Game::CELL), mgy = (int)(mw.y / Game::CELL);

        // --- Ввод: строительство и найм ---
        if (IsKeyPressed(KEY_ESCAPE)) buildSel = -1;
        for (int i = 0; i < (int)BType::Count; i++)
            if (IsKeyPressed(KEY_ONE + i)) buildSel = (buildSel == i) ? -1 : i;

        bool clickedUI = mScreen.y > (float)GetScreenHeight() - 96.f;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !clickedUI) {
            if (buildSel >= 0) {
                if (game.place((BType)buildSel, mgx, mgy)) buildSel = -1;
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) buildSel = -1;

        // найм: Q/W/E
        if (IsKeyPressed(KEY_Q)) game.hire(UType::Swordsman);
        if (IsKeyPressed(KEY_W)) game.hire(UType::Archer);
        if (IsKeyPressed(KEY_E)) game.hire(UType::Crossbowman);

        // выбор буста: клик или 1/2/3
        if (game.phase == Phase::Boost) {
            for (int i = 0; i < 3; i++)
                if (IsKeyPressed(KEY_ONE + i)) game.applyBoost(i);
            // клик по карточке — проверяем в блоке отрисовки через хитбоксы
        }

        if (game.phase == Phase::GameOver && IsKeyPressed(KEY_R)) { game.reset(); buildSel = -1; }

        // --- Симуляция: фиксированный шаг ---
        if (game.phase != Phase::Boost && game.phase != Phase::GameOver) {
            acc += GetFrameTime();
            if (acc > 0.25f) acc = 0.25f;
            while (acc >= DT) { game.tick(DT); acc -= DT; }
        }

        // ======================= ОТРИСОВКА =======================
        BeginDrawing();
        ClearBackground({ 34, 40, 34, 255 });

        BeginMode2D(cam);
        // Земля
        DrawRectangle(0, 0, (int)Game::MAP_PX, (int)Game::MAP_PX, { 48, 58, 46, 255 });
        // Сетка (каждые 2 клетки)
        for (int i = 0; i <= 48; i += 2) {
            float c = i * Game::CELL;
            DrawLine((int)c, 0, (int)c, (int)Game::MAP_PX, { 255, 255, 255, 12 });
            DrawLine(0, (int)c, (int)Game::MAP_PX, (int)c, { 255, 255, 255, 12 });
        }
        DrawRectangleLines(0, 0, (int)Game::MAP_PX, (int)Game::MAP_PX, { 90, 100, 90, 255 });

        // Здания
        for (const auto& b : game.buildings) {
            if (!b.alive) continue;
            int t = (int)b.type;
            float x = b.pos.x - Game::BSIZE / 2, y = b.pos.y - Game::BSIZE / 2;
            DrawRectangle((int)x, (int)y, (int)Game::BSIZE, (int)Game::BSIZE, BColors[t]);
            DrawRectangleLines((int)x, (int)y, (int)Game::BSIZE, (int)Game::BSIZE, { 20, 20, 20, 160 });
            hpBar(x + 8, y - 7, Game::BSIZE - 16, b.hp, b.maxHp);
        }

        // Юниты
        for (const auto& u : game.units) {
            int t = (int)u.type;
            DrawCircle((int)u.pos.x, (int)u.pos.y, 10, UColors[t]);
            DrawCircleLines((int)u.pos.x, (int)u.pos.y, 10, { 15, 15, 25, 255 });
            hpBar(u.pos.x - 10, u.pos.y - 16, 20, u.hp, u.maxHp);
        }

        // Враги
        for (const auto& e : game.enemies) {
            int t = (int)e.type;
            DrawCircle((int)e.pos.x, (int)e.pos.y, t == 1 ? 16.f : 12.f, EColors[t]);
            DrawCircleLines((int)e.pos.x, (int)e.pos.y, t == 1 ? 16.f : 12.f, { 40, 10, 10, 255 });
            hpBar(e.pos.x - 12, e.pos.y - 22, 24, e.hp, e.maxHp);
        }

        // Призрак постройки
        if (buildSel >= 0 && mgx >= 0 && mgy >= 0 && mgx < 47 && mgy < 47) {
            bool ok = game.canPlace((BType)buildSel, mgx, mgy);
            Color gc = ok ? Color{ 80, 220, 120, 110 } : Color{ 220, 80, 80, 110 };
            DrawRectangle(mgx * (int)Game::CELL, mgy * (int)Game::CELL, (int)Game::BSIZE, (int)Game::BSIZE, gc);
        }
        EndMode2D();

        // ======================= HUD =======================
        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        char buf[128];

        // Верхняя панель: ресурсы
        DrawRectangle(0, 0, sw, 30, { 0, 0, 0, 150 });
        DrawRectangle(10, 9, 12, 12, { 150, 100, 50, 255 });
        snprintf(buf, sizeof buf, "%d", (int)game.wood); txt(buf, 28, 7, 18, RAYWHITE);
        DrawRectangle(100, 9, 12, 12, { 200, 180, 60, 255 });
        snprintf(buf, sizeof buf, "%d", (int)game.food); txt(buf, 118, 7, 18, RAYWHITE);
        DrawRectangle(190, 9, 12, 12, { 220, 200, 90, 255 });
        snprintf(buf, sizeof buf, "%d", (int)game.gold); txt(buf, 208, 7, 18, RAYWHITE);
        snprintf(buf, sizeof buf, "%d/%d", game.pop, game.popCap); txt(buf, 280, 7, 18, SKYBLUE);

        // Фаза и волна — по центру
        if (game.phase == Phase::Day)
            snprintf(buf, sizeof buf, "День %d  |  до ночи %.0f с", game.wave + 1, game.phaseTimer);
        else if (game.phase == Phase::Night)
            snprintf(buf, sizeof buf, "НОЧЬ — волна %d  |  врагов: %zu", game.wave, game.enemies.size());
        else snprintf(buf, sizeof buf, "Выбор буста");
        txtC(buf, sw / 2, 7, 18, game.phase == Phase::Night ? RED : RAYWHITE);

        snprintf(buf, sizeof buf, "%d FPS | юнитов %zu | врагов %zu", GetFPS(), game.units.size(), game.enemies.size());
        txt(buf, sw - txtW(buf, 16) - 10, 7, 16, { 200, 200, 200, 220 });

        // Нижняя панель: постройки (1-7)
        float barY = sh - 96.f;
        DrawRectangle(0, barY, sw, 96, { 0, 0, 0, 170 });
        for (int i = 0; i < (int)BType::Count; i++) {
            if (i == (int)BType::TownCenter) continue; // ратуша строить нельзя
            float x = 10.f + (i > 0 ? i - 1 : 0) * 118.f;
            bool afford = game.canAfford((BType)i);
            bool sel = buildSel == i;
            DrawRectangle((int)x, (int)(barY + 8), 112, 80, sel ? Color{ 70, 90, 120, 255 } : Color{ 45, 48, 55, 255 });
            DrawRectangleLines((int)x, (int)(barY + 8), 112, 80, afford ? Color{ 120, 130, 150, 255 } : Color{ 70, 70, 70, 255 });
            DrawRectangle((int)x + 6, (int)(barY + 14), 14, 14, BColors[i]);
            snprintf(buf, sizeof buf, "%d", i + 1); txt(buf, x + 96, barY + 12, 16, GRAY);
            txt(bName((BType)i), x + 26, barY + 12, 15, afford ? RAYWHITE : GRAY);
            snprintf(buf, sizeof buf, "%gw %gf %gg", bCostWood((BType)i), bCostFood((BType)i), bCostGold((BType)i));
            txt(buf, x + 8, barY + 62, 14, afford ? Color{ 200, 200, 170, 255 } : Color{ 120, 100, 100, 255 });
        }

        // Найм (Q/W/E) — если есть казарма
        bool hasRax = false;
        for (const auto& b : game.buildings) if (b.alive && b.type == BType::Barracks) hasRax = true;
        if (hasRax) {
            float hx = 10.f + 6 * 118.f + 20.f;
            const char* keys = "QWE";
            for (int i = 0; i < (int)UType::Count; i++) {
                bool locked = (i == (int)UType::Crossbowman) && !game.crossbowUnlocked;
                float x = hx + i * 118.f;
                bool afford = game.food >= uCostFood((UType)i) * game.unitCostMult &&
                              game.gold >= uCostGold((UType)i) * game.unitCostMult && game.pop < game.popCap;
                DrawRectangle((int)x, (int)(barY + 8), 112, 80, { 45, 48, 55, 255 });
                DrawRectangleLines((int)x, (int)(barY + 8), 112, 80,
                    (!locked && afford) ? Color{ 120, 150, 130, 255 } : Color{ 70, 70, 70, 255 });
                DrawCircle((int)x + 14, (int)(barY + 21), 7, UColors[i]);
                txt(TextFormat("%c", keys[i]), x + 96, barY + 12, 16, GRAY);
                txt(locked ? "???" : uName((UType)i), x + 26, barY + 12, 15,
                    (!locked && afford) ? RAYWHITE : GRAY);
                if (!locked) {
                    snprintf(buf, sizeof buf, "%gf %gg", uCostFood((UType)i) * game.unitCostMult,
                             uCostGold((UType)i) * game.unitCostMult);
                    txt(buf, x + 8, barY + 62, 14, { 200, 200, 170, 255 });
                } else txt("буст", x + 8, barY + 62, 14, { 150, 110, 170, 255 });
            }
        }

        // ======================= ЭКРАН БУСТОВ =======================
        if (game.phase == Phase::Boost) {
            DrawRectangle(0, 0, (int)sw, (int)sh, { 0, 0, 0, 190 });
            txtC("Волна отбита! Выбери буст:", sw / 2, sh / 2 - 130, 30, GOLD);
            float cw = 260.f, ch = 170.f, gap = 30.f;
            float x0 = sw / 2 - (cw * 3 + gap * 2) / 2;
            for (int i = 0; i < 3; i++) {
                float x = x0 + i * (cw + gap), y = sh / 2 - 80;
                Rectangle r{ x, y, cw, ch };
                bool hov = CheckCollisionPointRec(mScreen, r);
                DrawRectangleRec(r, hov ? Color{ 70, 75, 95, 255 } : Color{ 50, 54, 70, 255 });
                DrawRectangleLinesEx(r, 2, hov ? GOLD : Color{ 100, 105, 130, 255 });
                txtC(game.boostName(game.offer[i]), x + cw / 2, y + 22, 22, RAYWHITE);
                txtC(game.boostDesc(game.offer[i]), x + cw / 2, y + 70, 16, { 190, 195, 210, 255 });
                char k[4]; snprintf(k, sizeof k, "%d", i + 1);
                txtC(k, x + cw / 2, y + ch - 34, 20, GRAY);
                if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) game.applyBoost(i);
            }
        }

        // ======================= GAME OVER =======================
        if (game.phase == Phase::GameOver) {
            DrawRectangle(0, 0, (int)sw, (int)sh, { 30, 0, 0, 200 });
            txtC("РАТУША ПАЛА", sw / 2, sh / 2 - 70, 44, RED);
            snprintf(buf, sizeof buf, "Ты продержался %d волн, убито врагов: %d", game.wave, game.enemiesKilled);
            txtC(buf, sw / 2, sh / 2, 22, RAYWHITE);
            txtC("R — начать заново", sw / 2, sh / 2 + 50, 20, GRAY);
        }

        EndDrawing();
    }

    if (gFontLoaded) UnloadFont(gFont);
    CloseWindow();
    return 0;
}
