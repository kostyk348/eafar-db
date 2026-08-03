// demos/galaxy_architect/main.cpp — Galaxy Architect.
// Phase 1: sparse procedural galaxy + two rival factions + ASCII renderer.
// Phase 2: faction AI automata (EXPLORE/EXPAND/WAR/RETREAT), fleet war, fog of war.
// EAFAR integration points (each is a comment where EAFAR-DB would replace std::map in prod):
//   Galaxy::cache_  → EAFAR-DB Table<system> (sparse pages, S2 thesis)
//   Journal         → all galaxy events (S3, S8+S9 time-travel)
//   View<population>→ empire aggregate (S5, lazy incremental)
//   Faction automaton→ hierarchical automaton (S6, Phase 3)
//   Fuzzy diplomacy  → membership predicates (S7, Phase 4)
//   replay_at        → watch past empires (S9)

#include "galaxy.hpp"
#include <cstdio>
#include <cstring>

using namespace std;

// ─── command enum ──────────────────────────────────────────
enum Cmd {
    CMD_MOVE_NW=0, CMD_MOVE_N=1, CMD_MOVE_NE=2,
    CMD_MOVE_W =3,               CMD_MOVE_E =4,
    CMD_MOVE_SW=5, CMD_MOVE_S=6, CMD_MOVE_SE=7,
    CMD_COLONIZE=8, CMD_WAIT=9, CMD_HELP=10, CMD_QUIT=11, CMD_REPLAY=12,
    CMD_STATUS=13,
    CMD_COUNT
};
static const int dx[] = {-1,-1,-1, 0, 0, 1, 1, 1};
static const int dy[] = {-1, 0, 1,-1, 1,-1, 0, 1};
static_assert(CMD_COUNT == 14, "expect 14 command slots");

struct Game {
    Galaxy galaxy{0xCAFEBABE};
    int player_x = 0, player_y = 0;
    int turn = 0;
    vector<pair<int,int>> colonized;
};

static void print_fleet_status(const Galaxy& g) {
    for (auto& f : g.fleets()) {
        const char* who = f.faction == 0 ? "PLAYER"
            : g.factions()[static_cast<size_t>(f.faction) - 1].name.c_str();
        printf("  fleet[%lld] %s str=%d -> (%d,%d) in %d turns\n",
               (long long)f.id, who, f.strength, f.target_x, f.target_y, f.turns_left);
    }
}

static void print_status(const Game& g) {
    printf("--- status (turn %d) ---\n", g.turn);
    for (auto& f : g.galaxy.factions()) {
        const char* st = f.state == Faction::EXPLORE ? "EXPLORE" :
                         f.state == Faction::EXPAND  ? "EXPAND"  :
                         f.state == Faction::WAR     ? "WAR"     : "RETREAT";
        printf("  %s [%c] %-12s held=%d tech=%.2f\n",
               f.name.c_str(), f.glyph, st, f.systems_held, f.avg_tech);
    }
    printf("  player colonies: %zu  fleets: %zu\n",
           g.colonized.size(), g.galaxy.fleets().size());
    print_fleet_status(g.galaxy);
}

static bool process_cmd(Game& g, int cmd, int& cx, int& cy, int range) {
    if (cmd < 0 || cmd > CMD_STATUS) {
        printf("Unknown command. Type 'help'.\n");
        return true;
    }
    if (cmd == CMD_QUIT) return false;

    if (cmd >= CMD_MOVE_NW && cmd <= CMD_MOVE_SE) {
        cx += dx[cmd]; cy += dy[cmd];
        for (int ddy = -range; ddy <= range; ++ddy)
            for (int ddx = -range; ddx <= range; ++ddx)
                g.galaxy.probe(cx + ddx, cy + ddy);
        printf("Moved to (%d,%d). Systems probed: %zu\n",
               cx, cy, g.galaxy.materialized_count());
    } else if (cmd == CMD_COLONIZE) {
        Star& s = g.galaxy.get_system(cx, cy);
        if (s.colonized) printf("System (%d,%d) already colonized.\n", cx, cy);
        else if (s.type == 4) printf("Black hole (%d,%d) — cannot colonize.\n", cx, cy);
        else {
            s.colonized = true; s.owner_faction = 0; s.population = 10; s.defense = 3;
            g.colonized.push_back({cx, cy});
            printf("Colonized (%d,%d). Your empire: %zu systems.\n", cx, cy, g.colonized.size());
            // EAFAR-DB: journal records this → replay_at replays your entire colonization history.
        }
    } else if (cmd == CMD_WAIT) {
        printf("Time passes.\n");
    } else if (cmd == CMD_HELP) {
        printf("Commands: NW N NE W E SW S SE | colonize | wait | status | help | quit | replay\n");
    } else if (cmd == CMD_REPLAY) {
        printf("--- Time-travel (S9) ---\n");
        printf("EAFAR-DB replay_at(t) reconstructs empire state at any past moment.\n");
        printf("Phase 4 demo: enter turn number.\n");
    } else if (cmd == CMD_STATUS) {
        print_status(g);
    }
    return true;
}

int main() {
    Game g;
    int range = 6;
    int cx = 0, cy = 0;

    Faction fa; fa.name="Iron Concord"; fa.state=Faction::EXPLORE; fa.war_threshold=0.55f;
        fa.home_x = -4; fa.home_y = -4; fa.glyph = 'a';
        g.galaxy.factions().push_back(fa);
        Faction fb; fb.name="Void Syndicate"; fb.state=Faction::EXPLORE; fb.war_threshold=0.65f;
        fb.home_x = 4; fb.home_y = 4; fb.glyph = 'v';
        g.galaxy.factions().push_back(fb);
    for (int ddy = -range; ddy <= range; ++ddy)
        for (int ddx = -range; ddx <= range; ++ddx)
            g.galaxy.probe(ddx, ddy);

    // spawn homeworlds for the factions
    Star& h1 = g.galaxy.get_system(-4, -4);
    h1.colonized = true; h1.owner_faction = 1; h1.population = 20; h1.defense = 5;
    Star& h2 = g.galaxy.get_system(4, 4);
    h2.colonized = true; h2.owner_faction = 2; h2.population = 20; h2.defense = 5;

    printf("═══════════════════════════════════════════════════\n");
    printf("  Galaxy Architect Phase 2 — faction AI + war + fog\n");
    printf("═══════════════════════════════════════════════════\n");
    printf("Moves: NW N NE  W . E  SW S SE  |  colonize | wait | status | help | quit | replay\n");
    printf("sparse: only explored regions materialized (S2 thesis)\n");
    printf("factions act each turn: explore -> expand -> war; fog hides enemy activity\n");
    printf("═══════════════════════════════════════════════════\n\n");

    bool running = true;
    while (running) {
        g.galaxy.update_vision(cx, cy, range);
        render_galaxy(g.galaxy, cx, cy, range);
        printf("turn=%d  pos=(%d,%d)  colonies=%zu  systems=%zu  fleets=%zu\n",
               g.turn, cx, cy, g.colonized.size(),
               g.galaxy.materialized_count(), g.galaxy.fleets().size());
        printf("> ");
        char buf[64] = {};
        if (!fgets(buf, sizeof(buf), stdin)) break;
        buf[strcspn(buf, "\r\n")] = 0;

        int cmd = -1;
        struct { const char* n; int c; } dir_map[] = {
            {"nw",CMD_MOVE_NW},{"n",CMD_MOVE_N},{"ne",CMD_MOVE_NE},
            {"w",CMD_MOVE_W},  {"e",CMD_MOVE_E},
            {"sw",CMD_MOVE_SW},{"s",CMD_MOVE_S},{"se",CMD_MOVE_SE},
            {"a",CMD_MOVE_W},  {"d",CMD_MOVE_E},
            {"q",CMD_MOVE_NW}, {"r",CMD_MOVE_NE}, {"z",CMD_MOVE_SW}, {"c",CMD_MOVE_SE},
            {"h",CMD_MOVE_W},  {"j",CMD_MOVE_S}, {"k",CMD_MOVE_N}, {"l",CMD_MOVE_E},
        };
        // Direct commands
        if      (strcmp(buf, "colonize")==0) cmd = CMD_COLONIZE;
        else if (strcmp(buf, "wait")==0)      cmd = CMD_WAIT;
        else if (strcmp(buf, "help")==0)      cmd = CMD_HELP;
        else if (strcmp(buf, "quit")==0)      cmd = CMD_QUIT;
        else if (strcmp(buf, "replay")==0)    cmd = CMD_REPLAY;
        else if (strcmp(buf, "status")==0)    cmd = CMD_STATUS;
        else {
            for (auto& dm : dir_map) {
                if (strcmp(buf, dm.n) == 0) { cmd = dm.c; break; }
            }
        }
        if (cmd == -1) { printf("Unknown '%s'. Type 'help'.\n", buf); continue; }
        running = process_cmd(g, cmd, cx, cy, range);
        // factions act after the player's action; log up to 4 combat/action lines
        int log_count = 4;
        g.galaxy.tick_automata(log_count);
        g.turn++;
    }
    printf("Empire held %zu systems over %d turns.\n", g.colonized.size(), g.turn);
    return 0;
}
