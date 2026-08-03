#pragma once
// galaxy.hpp — sparse procedural galaxy for Galaxy Architect demo.
// EAFAR analogy: each system = a sparse page (materialized on probe).
// Unvisited systems never exist (zero cost).
//
// Phase 2 additions:
//   - Fleet travel + combat (war)   — EAFAR events, deterministic order
//   - Faction AI automaton          — per-faction state machine (EXPLORE/EXPAND/WAR/RETREAT)
//   - Fog of war                    — enemy ownership hidden outside player vision

#include <cstdint>
#include <cmath>
#include <random>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <cstdio>
#include <algorithm>

struct Star {
    int64_t  id;
    int32_t  x, y;
    uint8_t  type;           // 0=red dwarf..4=black hole
    float    habitability;
    bool     colonized;
    int16_t  owner_faction;  // -1 = neutral; 0 = player; 1..n = faction idx+1
    int64_t  population;
    int32_t  defense = 0;    // garrison strength built by factions/player
};

struct Fleet {
    int64_t id;
    int  origin_x, origin_y, target_x, target_y;
    int32_t strength;
    int  faction;            // 0 = player, 1..n = faction idx+1
    int  turns_left;         // turns until arrival
};

struct Faction {
    enum State { EXPLORE, EXPAND, WAR, RETREAT } state;
    float  war_threshold = 0.6f;   // go to war when pressure > threshold
    int    systems_held = 0;
    float  avg_tech = 0.0f;
    int    home_x = 0, home_y = 0; // homeworld (spawn)
    char   glyph = 'a';            // render letter
    int    retreat_timer = 0;      // turns spent regrouping in RETREAT
    std::string name;
};

class Galaxy {
public:
    Galaxy(int64_t seed = 0xCAFEBABE) : seed_(seed), rng_(static_cast<uint32_t>(seed)) {}

    // Materialize (generate) a system on demand. Sparse: unprobed systems don't exist.
    Star& get_system(int x, int y) {
        int64_t key = system_key(x, y);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        Star s;
        s.id = key; s.x = x; s.y = y;
        uint32_t h = pcg(rng_);
        s.type = h % 5;
        s.habitability = rand_float(rng_);
        s.colonized = false;
        s.owner_faction = -1;
        s.population = 0;
        auto res = cache_.emplace(key, s);
        return res.first->second;
    }

    // Probe: mark as explored + materialize the star (sparse: unprobed = absent).
    void probe(int x, int y) { get_system(x, y); probed_.insert(system_key(x, y)); }
    bool is_probed(int x, int y) const { return probed_.count(system_key(x, y)) > 0; }
    std::size_t materialized_count() const { return cache_.size(); }

    // Peek at a probed system (const; used by renderer). Returns nullptr if not probed.
    const Star* peek(int x, int y) const {
        auto it = cache_.find(system_key(x, y));
        if (it == cache_.end()) return nullptr;
        return &it->second;
    }

    std::vector<Fleet>& fleets() { return fleets_; }
    std::vector<Faction>& factions() { return factions_; }
    const std::vector<Fleet>& fleets() const { return fleets_; }
    const std::vector<Faction>& factions() const { return factions_; }
    int64_t seed() const { return seed_; }

    // ── Phase 2: fog of war ─────────────────────────────────────────────
    // Player vision = radius `view` around player pos + radius 2 around each player colony.
    void update_vision(int px, int py, int view) {
        vision_.clear();
        auto add = [&](int x, int y, int r) {
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    vision_.insert(system_key(x + dx, y + dy));
        };
        add(px, py, view);
        for (auto& [key, s] : cache_)
            if (s.colonized && s.owner_faction == 0)
                add(s.x, s.y, 2);
    }
    bool in_vision(int x, int y) const { return vision_.count(system_key(x, y)) > 0; }

    // ── Phase 2: fleet helpers ──────────────────────────────────────────
    // Total fleet strength of a faction currently in flight (garrisons excluded).
    int fleet_power_in_flight(int fi) const {
        int p = 0;
        for (auto& fl : fleets_) if (fl.faction == fi) p += fl.strength;
        return p;
    }
    // Number of fleets a faction has in flight.
    int fleet_count_in_flight(int fi) const {
        int n = 0;
        for (auto& fl : fleets_) if (fl.faction == fi) ++n;
        return n;
    }
    // True if a fleet of faction fi is already flying to (tx,ty).
    bool fleet_en_route(int fi, int tx, int ty) const {
        for (auto& fl : fleets_)
            if (fl.faction == fi && fl.target_x == tx && fl.target_y == ty) return true;
        return false;
    }
    // Total defensive strength present at (x,y): garrison + population/5 + landed fleets.
    int defense_at(int x, int y) const {
        int d = 0;
        if (const Star* s = peek(x, y)) {
            d += s->defense;
            d += static_cast<int>(s->population / 5);
        }
        for (auto& fl : fleets_)
            if (fl.turns_left <= 0 && fl.target_x == x && fl.target_y == y)
                d += fl.strength;
        return d;
    }

    // ── Phase 2: faction AI automaton (called once per turn) ────────────
    void tick_automata(int& log_count) {
        // 1) advance in-flight fleets; arrivals resolve combat immediately
        std::vector<std::size_t> arrivals;
        for (std::size_t i = 0; i < fleets_.size(); ++i) {
            Fleet& fl = fleets_[i];
            if (fl.turns_left > 0) {
                fl.turns_left--;
                if (fl.turns_left == 0) arrivals.push_back(i);
            }
        }
        for (std::size_t idx : arrivals) resolve_combat(fleets_[idx], log_count);

        // 2) per-faction automaton step (factions act after player)
        for (std::size_t i = 0; i < factions_.size(); ++i)
            tick_faction(static_cast<int>(i) + 1, log_count);

        // 3) clean dead fleets (strength <= 0) after all combat resolved
        fleets_.erase(std::remove_if(fleets_.begin(), fleets_.end(),
                                     [](const Fleet& fl){ return fl.strength <= 0; }),
                      fleets_.end());
    }

    void tick_faction(int fi, int& log_count) {
        Faction& f = factions_[static_cast<std::size_t>(fi) - 1];
        // sensor sweep: factions "wake up" sleeping sparse pages around their colonies,
        // exactly like the player does on movement (sparse exploration, S2 thesis)
        const int sensor = 2;
        std::vector<Star*> own;
        for (auto& [key, s] : cache_)
            if (s.colonized && s.owner_faction == fi) {
                own.push_back(&s);
                for (int dy = -sensor; dy <= sensor; ++dy)
                    for (int dx = -sensor; dx <= sensor; ++dx)
                        probe(s.x + dx, s.y + dy);
            }
        f.systems_held = static_cast<int>(own.size());

        // pressure: border conflict — any enemy system within radius 4 of our empire
        bool border = false;
        for (auto* s : own) {
            for (int dy = -4; dy <= 4; ++dy)
                for (int dx = -4; dx <= 4; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const Star* n = peek(s->x + dx, s->y + dy);
                    if (!n) continue;
                    if (n->colonized && n->owner_faction != fi) { border = true; goto done; }
                }
        }
    done:
        float pressure = border ? 1.0f : 0.0f;

        // state transitions
        if (f.state == Faction::EXPLORE && own.size() >= 2) f.state = Faction::EXPAND;
        if (f.state == Faction::EXPAND && pressure > f.war_threshold) f.state = Faction::WAR;
        if (f.state == Faction::WAR) {
            if (own.empty() || fleet_power_in_flight(fi) < 5) f.state = Faction::RETREAT;
            else if (!border) f.state = Faction::EXPAND;   // no enemies nearby -> resume expansion
        }
        if (f.state == Faction::RETREAT) {
            f.retreat_timer++;
            // regrouping is time-boxed: after 3 turns, resume activity regardless of fleet power
            if (f.retreat_timer >= 3) f.state = Faction::EXPAND;
        } else {
            f.retreat_timer = 0;
        }

        // act per state
        const int fleet_cap = 8;  // max fleets a faction keeps in flight
        switch (f.state) {
        case Faction::EXPLORE: {
            // send a small colonizer to the nearest unowned habitable system near any own system
            Star* best = nullptr; int best_d = INT32_MAX;
            for (auto& [key, s] : cache_) {
                if (s.colonized || s.type == 4 || s.habitability <= 0.35f) continue; // uncolonizable/barren
                for (auto* o : own) {
                    int d = std::abs(s.x - o->x) + std::abs(s.y - o->y);
                    if (d < best_d) { best_d = d; best = &s; }
                }
            }
            // also allow scanning unknown space: probe a random nearby empty cell
            if (best && best_d <= 10 && !fleet_en_route(fi, best->x, best->y) &&
                fleet_count_in_flight(fi) < fleet_cap) {
                spawn_fleet(own[0]->x, own[0]->y, best->x, best->y, 3, fi,
                            best_d > 3 ? best_d : 2);
                if (log_count) { printf("[%s] explores (%d,%d)\n", f.name.c_str(), best->x, best->y); --log_count; }
            }
            break;
        }
        case Faction::EXPAND: {
            // grow + build garrison on border systems
            for (auto* s : own) {
                s->population += 1 + static_cast<int64_t>(s->habitability * 3);
                s->defense += 1;
            }
            Star* best = nullptr; int best_d = INT32_MAX;
            for (auto& [key, s] : cache_) {
                if (s.colonized || s.type == 4 || s.habitability <= 0.35f) continue;
                for (auto* o : own) {
                    int d = std::abs(s.x - o->x) + std::abs(s.y - o->y);
                    if (d < best_d) { best_d = d; best = &s; }
                }
            }
            if (best && best_d <= 7 && !fleet_en_route(fi, best->x, best->y) &&
                fleet_count_in_flight(fi) < fleet_cap) {
                spawn_fleet(own[0]->x, own[0]->y, best->x, best->y, 4, fi, best_d);
                if (log_count) { printf("[%s] expands -> (%d,%d)\n", f.name.c_str(), best->x, best->y); --log_count; }
            }
            break;
        }
        case Faction::WAR: {
            // attack nearest enemy system (player or other faction)
            Star* target = nullptr; int best_d = INT32_MAX; int t_owner = -2;
            for (auto& [key, s] : cache_) {
                if (!s.colonized || s.owner_faction == fi) continue;
                for (auto* o : own) {
                    int d = std::abs(s.x - o->x) + std::abs(s.y - o->y);
                    if (d < best_d) { best_d = d; target = &s; t_owner = s.owner_faction; }
                }
            }
            if (target && !fleet_en_route(fi, target->x, target->y) &&
                fleet_count_in_flight(fi) < fleet_cap) {
                // gather a strike force: up to 10 + 2/held
                int str = 6 + static_cast<int>(own.size()) * 2;
                spawn_fleet(own[0]->x, own[0]->y, target->x, target->y, str, fi,
                            best_d > 1 ? best_d : 1);
                if (log_count) {
                    const char* foe = t_owner == 0 ? "PLAYER" : factions_[static_cast<std::size_t>(t_owner) - 1].name.c_str();
                    printf("[%s] WAR: striking %s at (%d,%d) with %d\n",
                           f.name.c_str(), foe, target->x, target->y, str);
                    --log_count;
                }
            }
            break;
        }
        case Faction::RETREAT: {
            // regroup: build fleets at homeworld, then resume expansion
            if (f.retreat_timer == 0) {
                // pull in-flight fleets back to homeworld (single recall wave)
                for (auto& fl : fleets_) {
                    if (fl.faction == fi) {
                        fl.origin_x = fl.target_x; fl.origin_y = fl.target_y;
                        fl.target_x = f.home_x; fl.target_y = f.home_y;
                        fl.turns_left = std::max(1, std::abs(f.home_x - fl.target_x) + std::abs(f.home_y - fl.target_y));
                    }
                }
            }
            // build new fleet for the push-back
            if (fleet_count_in_flight(fi) < 4) {
                spawn_fleet(f.home_x, f.home_y, f.home_x + 1, f.home_y, 6, fi, 2);
                if (log_count) { printf("[%s] regroups fleet at homeworld\n", f.name.c_str()); --log_count; }
            }
            break;
        }
        }
    }

    void spawn_fleet(int ox, int oy, int tx, int ty, int32_t strength, int fi, int travel) {
        static int64_t fid = 1;
        Fleet fl;
        fl.id = fid++; fl.origin_x = ox; fl.origin_y = oy;
        fl.target_x = tx; fl.target_y = ty;
        fl.strength = strength; fl.faction = fi;
        fl.turns_left = std::max(1, travel);
        fleets_.push_back(fl);

    }

private:
    void resolve_combat(Fleet& fl, int& log_count) {
        Star& target = get_system(fl.target_x, fl.target_y);
        const char* att_name = fl.faction == 0 ? "PLAYER"
            : factions_[static_cast<std::size_t>(fl.faction) - 1].name.c_str();
        if (!target.colonized) {
            // uncolonized: colonize if habitability decent; else disperse
            if (target.type != 4 && target.habitability > 0.35f) {
                target.colonized = true;
                target.owner_faction = fl.faction;
                target.population = fl.strength * 2;
                target.defense = 2;
                if (log_count) { printf("[%s] colonized (%d,%d) via fleet\n", att_name, fl.target_x, fl.target_y); --log_count; }
                fl.strength = 0; // fleet converted into colony
            } else {
                fl.strength = 0; // dispersed on barren world
            }
            return;
        }
        if (target.owner_faction == fl.faction) {
            // reinforcing own system: strength joins garrison
            target.defense += fl.strength;
            fl.strength = 0;
            return;
        }
        // enemy-held: combat
        int def = defense_at(fl.target_x, fl.target_y);
        const char* def_name = target.owner_faction == 0 ? "PLAYER"
            : factions_[static_cast<std::size_t>(target.owner_faction) - 1].name.c_str();
        if (fl.strength > def) {
            int loss = def / 2;
            int survivors = fl.strength - loss;
            if (log_count) {
                printf("⚔ %s defeated %s at (%d,%d): fleet %d vs defense %d, survivors %d\n",
                       att_name, def_name, fl.target_x, fl.target_y, fl.strength, def, survivors);
                --log_count;
            }
            target.owner_faction = fl.faction;
            target.population = std::max<int64_t>(1, target.population / 3);
            target.defense = survivors / 2;
            fl.strength = 0;
        } else {
            // defender wins; attacker destroyed, defender loses half of its defense value
            if (log_count) {
                printf("⚔ %s repelled by %s at (%d,%d): fleet %d vs defense %d\n",
                       att_name, def_name, fl.target_x, fl.target_y, fl.strength, def);
                --log_count;
            }
            target.defense = std::max(0, target.defense - fl.strength / 2);
            if (target.owner_faction == 0) target.population = std::max<int64_t>(0, target.population - fl.strength * 3);
            fl.strength = 0;
        }
    }

    static int64_t system_key(int x, int y) {
        return (static_cast<int64_t>(x) << 32) |
                (static_cast<uint32_t>(static_cast<int32_t>(y)));
    }

    static uint32_t pcg(uint32_t& state) {
        state = state * 747796405u + 2891336453u;
        uint32_t xorshifted = ((state ^ (state >> 18u)) >> 27u);
        uint32_t rot = (state >> 5u) & 31u;
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }
    static float rand_float(uint32_t& rng) { return pcg(rng) / 4294967295.0f; }

    int64_t seed_;
    uint32_t rng_;
    std::map<int64_t, Star> cache_;   // materialized systems only
    std::set<int64_t> probed_;        // which systems were probed
    std::set<int64_t> vision_;        // fog-of-war visibility set
    std::vector<Fleet> fleets_;
    std::vector<Faction> factions_;
};

// ASCII renderer with fog of war: enemy ownership only shown inside vision.
inline void render_galaxy(const Galaxy& gal, int cx, int cy, int range) {
    for (int i = 0; i < range * 2 + 3; ++i) printf("─");
    printf(" Galaxy Architect  seed=0x%llx view=%d ─\n",
           (unsigned long long)gal.seed(), range);

    for (int dy = -range; dy <= range; ++dy) {
        printf("│");
        for (int dx = -range; dx <= range; ++dx) {
            int wx = cx + dx, wy = cy + dy;
            if (dx == 0 && dy == 0) { printf("@"); continue; }  // player
            const Star* s = gal.peek(wx, wy);
            if (!s) { printf(" "); continue; }  // unexplored = sparse dark matter
            if (!s->colonized) {
                if (s->type == 4) printf("X");   // black hole always visible once probed
                else printf("+");
                continue;
            }
            // colonized: player = digit, factions = letter; hidden by fog if out of vision
            bool vis = gal.in_vision(wx, wy);
            if (s->owner_faction == 0) {
                printf("%c", vis ? static_cast<char>('0' + s->type) : '?');
            } else {
                char g = gal.factions().empty() ? 'f'
                    : gal.factions()[static_cast<std::size_t>(s->owner_faction) - 1].glyph;
                printf("%c", vis ? g : '?');
            }
        }
        printf("│\n");
    }

    for (int i = 0; i < range * 2 + 3; ++i) printf("─");
    printf("\n");
}
