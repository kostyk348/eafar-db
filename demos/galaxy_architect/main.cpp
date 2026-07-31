// demos/galaxy_architect/main.cpp — Galaxy Architect Phase 1 skeleton.
// Sparse procedurally generated galaxy + two rival factions + ASCII renderer.
// EAFAR integration points (each is a comment where EAFAR-DB would replace std::map in prod):
//   Galaxy::cache_  → EAFAR-DB Table<system> (sparse pages, S2 thesis)
//   Journal         → all galaxy events (S3, S8+S9 time-travel)
//   View<population>→ empire aggregate (S5, lazy incremental)
//   Faction automaton→ hierarchical automaton (S6, Phase 3)
//   Fuzzy diplomacy  → membership predicates (S7)
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
    CMD_COUNT
};
static const int dx[] = {-1,-1,-1, 0, 0, 1, 1, 1};
static const int dy[] = {-1, 0, 1,-1, 1,-1, 0, 1};
static_assert(CMD_COUNT == 13, "expect 13 command slots");

struct Game {
    Galaxy galaxy{0xCAFEBABE};
    int player_x = 0, player_y = 0;
    int turn = 0;
    vector<pair<int,int>> colonized;
};

static bool process_cmd(Game& g, int cmd, int& cx, int& cy, int range) {
    if (cmd < 0 || cmd > CMD_REPLAY) {
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
        Star& s = const_cast<Star&>(g.galaxy.get_system(cx, cy));
        if (s.colonized) printf("System (%d,%d) already colonized.\n", cx, cy);
        else {
            s.colonized = true; s.owner_faction = -1; s.population = 1;
            g.colonized.push_back({cx, cy});
            printf("Colonized (%d,%d). Your empire: %zu systems.\n", cx, cy, g.colonized.size());
            // EAFAR-DB: journal records this → replay_at replays your entire colonization history.
        }
    } else if (cmd == CMD_WAIT) {
        printf("Time passes.\n");
    } else if (cmd == CMD_HELP) {
        printf("Commands: NW N NE W E SW S SE | colonize | wait | help | quit | replay\n");
    } else if (cmd == CMD_REPLAY) {
        printf("--- Time-travel (S9) ---\n");
        printf("EAFAR-DB replay_at(t) reconstructs empire state at any past moment.\n");
        printf("Phase 4 demo: enter turn number.\n");
    }
    return true;
}

int main() {
    Game g;
    int range = 6;
    int cx = 0, cy = 0;

    Faction fa; fa.name="Iron Concord"; fa.state=Faction::EXPLORE; fa.war_threshold=0.55f;
        g.galaxy.factions().push_back(fa);
        Faction fb; fb.name="Void Syndicate"; fb.state=Faction::EXPLORE; fb.war_threshold=0.65f;
        g.galaxy.factions().push_back(fb);
    for (int ddy = -range; ddy <= range; ++ddy)
        for (int ddx = -range; ddx <= range; ++ddx)
            g.galaxy.probe(ddx, ddy);

    printf("═══════════════════════════════════════════════════\n");
    printf("  Galaxy Architect Phase 1 — sparse galaxy + turn-based play\n");
    printf("═══════════════════════════════════════════════════\n");
    printf("Moves: NW N NE  W . E  SW S SE  |  colonize | wait | help | quit | replay\n");
    printf("sparse: only explored regions materialized (S2 thesis)\n");
    printf("═══════════════════════════════════════════════════\n\n");

    bool running = true;
    while (running) {
        render_galaxy(g.galaxy, cx, cy, range);
        printf("turn=%d  pos=(%d,%d)  colonies=%zu  systems_%zu  fleets=%zu\n",
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
        else {
            for (auto& dm : dir_map) {
                if (strcmp(buf, dm.n) == 0) { cmd = dm.c; break; }
            }
        }
        if (cmd == -1) { printf("Unknown '%s'. Type 'help'.\n", buf); continue; }
        running = process_cmd(g, cmd, cx, cy, range);
        g.turn++;
    }
    printf("Empire held %zu systems over %d turns.\n", g.colonized.size(), g.turn);
    return 0;
}
