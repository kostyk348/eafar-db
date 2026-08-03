# Galaxy Architect — demo for EAFAR-DB

Turn-based strategy demo showing the EAFAR paradigm on a sparse procedural galaxy.

## Phase 2 — faction AI + war + fog of war

```
Moves: NW N NE  W . E  SW S SE  |  colonize | wait | status | help | quit | replay
```

| Feature | What it shows | EAFAR principle |
|---|---|---|
| Sparse galaxy | systems materialize only on probe; `systems=169` from a 13×13 viewport | S2 sparse pages |
| Faction AI automaton | each faction is a state machine: EXPLORE → EXPAND → WAR → RETREAT | S6 automata / dependency |
| Fleet war | fleets travel turns, combat resolves on arrival: `⚔ attacker repelled by defender at (x,y): fleet N vs defense M` | S8 transactions (battle = atomic state change) |
| Fog of war | enemy ownership (`a`/`v`) only visible inside player vision; `?` = enemy colony out of sight | sparse + lazy visibility |
| Time-travel (stub) | `replay` command placeholder | S9 replay_at |

### Faction automaton (EAFAR state machine)

```
EXPLORE ──(2+ systems)──► EXPAND ──(enemy within 4)──► WAR
   ▲                        │                           │
   └────────────────────────┴──(no enemies)──(low power)┘
                               WAR ──(fleets < 5)──► RETREAT ──(3 turns)──► EXPAND
```

Faction behavior per turn:
- **Sensor sweep** — each colony probes radius-2 neighborhood (wakes sleeping sparse pages, same as player movement).
- **EXPLORE** — sends a 3-strength colonizer to the nearest unowned habitable system (≤10 away, black holes and barren worlds skipped).
- **EXPAND** — grows population (`+1 + habitability*3`), +1 garrison per colony, sends 4-strength colonizers (≤7 away).
- **WAR** — border conflict (enemy system within 4): strikes nearest enemy system with `6 + 2·systems_held` fleet.
- **RETREAT** — single recall wave to homeworld, then builds regrouping fleets; exits after 3 turns.

### Combat

- Arrival at unowned habitable system → colonize (`population = fleet*2`, garrison 2); barren/black hole → fleet disperses.
- Arrival at own system → fleet joins garrison.
- Arrival at enemy system → `attacker vs defender` (garrison + population/5 + landed fleets). Attacker wins → system flips owner, population /3. Defender wins → attacker destroyed, defender loses half its garrison.

### Determinism

Same seed (`0xcafebabe`) + same command sequence → identical galaxy, identical war history.
Deterministic PCG (SplitMix-style) for procedural generation; fleets and factions tick in fixed order.
