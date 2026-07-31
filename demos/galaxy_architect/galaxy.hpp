#pragma once
// galaxy.hpp — sparse procedural galaxy for Galaxy Architect demo.
// EAFAR analogy: each system = a sparse page (materialized on probe).
// Unvisited systems never exist (zero cost).

#include <cstdint>
#include <cmath>
#include <random>
#include <vector>
#include <string>
#include <map>
#include <set>

struct Star {
    int64_t  id;
    int32_t  x, y;
    uint8_t  type;           // 0=red dwarf..4=black hole
    float    habitability;
    bool     colonized;
    int16_t  owner_faction;  // -1 = neutral/owned by player in demo
    int64_t  population;
};

struct Fleet {
    int64_t id;
    int  origin_x, origin_y, target_x, target_y;
    int32_t strength;
    int  faction;
};

struct Faction {
    enum State { EXPLORE, EXPAND, WAR, RETREAT } state;
    float  war_threshold = 0.6f;
    int    systems_held = 0;
    float  avg_tech = 0.0f;
    std::string name;
};

class Galaxy {
public:
    Galaxy(int64_t seed = 0xCAFEBABE) : seed_(seed), rng_(static_cast<uint32_t>(seed)) {}

    // Materialize (generate) a system on demand. Sparse: unprobed systems don't exist.
    const Star& get_system(int x, int y) {
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
        cache_[key] = s;
        return cache_[key];
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
    int64_t seed() const { return seed_; }

private:
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
    std::vector<Fleet> fleets_;
    std::vector<Faction> factions_;
};

// ASCII renderer: shows viewport around (cx, cy).
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
            char c = '.';
            if (s->colonized) c = static_cast<char>('0' + s->type);
            else if (s->type == 4) c = 'X';  // black hole
            else c = '+';
            printf("%c", c);
        }
        printf("│\n");
    }

    for (int i = 0; i < range * 2 + 3; ++i) printf("─");
    printf("\n");
}
