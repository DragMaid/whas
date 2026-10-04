# Architecture

The client is C++20 on raylib (drawing, input, audio) and ImGui (panels).
Everything lives in a static library `whas`; `whasg` (the game) and
`whas_tests` link against it. The server is a separate .NET project.

## The world

One fixed-size grid of cells, `GRID_W x GRID_H` (`include/whas/constants.h`),
drawn `CELL_SIZE` pixels each. The view (`engine/view.cpp`) scales it to fit
the window.

| Part | Where | What it does |
|---|---|---|
| Cells | `core/cell.h`, `element/` | One element per cell with heat, hardness, lifetime and flags. Each element has an update function (`element/*.cpp`). |
| Simulation | `engine/simulation.cpp` | One tick: pressure, cells on worker threads (chunked, deterministic order), rigid bodies, spell effects, particles, heat. |
| Particles | `physics/particle_system.cpp` | Loose things in flight: spell projectiles, splashes, debris. Projectiles break cells by spending power; they hit hurtboxes (characters). |
| Rigid bodies | `physics/rigid_body_system.cpp` | Rock and ice become Box2D bodies unless anchored (`CELL_ANCHORED`) or held (`CELL_HELD`). |
| Sound | `audio/` | Listens to the world, never changes it: cells that move or come to rest, noises the particles log, and rigid bodies striking (one thump per rock or ice body, as loud as it hit; their cells and rock particles are silent). |
| Snapshots | `engine/snapshot.cpp` | The whole world as bytes, for desync recovery. Bump `MAGIC` when the layout changes. |

The simulation is deterministic: the same seed and inputs give the same
world on every machine. See [protocol.md](protocol.md) for the rules that
keep it that way.

## Characters

`game/character.cpp`. A body on top of the grid (never written into it):
walking, jumping, diving, launches, and status effects:

- **Burning**: stacks from fire cells, loose fire particles and fire bolts
  passing through; water clears it.
- **Wet paper**: water soaks the spell paper for `WET_SECONDS`. Only wind
  underfoot can be cast while wet; flying dries it three times faster and
  casting a flight dries it outright.
- **Unstuck**: loose grains inside the body are thrown off as particles
  (`Unbury`). Solid terrain that grows into a body slips it to the nearest
  free spot within `UNSTUCK_REACH` cells; deeper than that it's trapped and
  has to dig out. `PlaceClear` is the old "rise out of the ground" and is only
  used for spawning.

## Spells

`spell/`. A spell is glyphs placed in a circle. `SpellSystem::Evaluate`
turns them into `SpellStats`; `SpellQuant` rounds those to integers, which
is what every client simulates with. The server has its own copy of the
evaluator (`server/Whas.Server/Spells/SpellEvaluator.cs`) and both are
checked against `tests/fixtures/spells.json`. See [spells.md](spells.md).

## Game modes

| Mode | Where |
|---|---|
| Sandbox | `game/sandbox.cpp`: paint the world, test spells from a dummy |
| Duels | `game/game.cpp` (input, drawing), `game/match.cpp` (the deterministic rules), `game/turn_controller.cpp` (planned turns), `game/rts.cpp` (real time) |
| Replays | `game/replay*.cpp` |
| Maps | `game/map.cpp`, `ui/map_editor.cpp`, `ui/map_gallery.cpp` |
| Campaigns | `campaign/`; see [campaign.md](campaign.md) |

`main.cpp` owns one of each and decides which one updates and draws.

Casting goes through `CastTargeting` (`game/placement.cpp`), shared by every
mode: left click casts from the body, right click from the ground or wall
nearest the cursor.

## Networking

`net/lockstep_client.cpp` talks to the server over WebSockets. Clients
exchange plans (`net/plan_codec.cpp`), not world state; both run the same
ticks and compare hashes. Details in [protocol.md](protocol.md).

## Server

`server/Whas.Server`: ASP.NET with Postgres (EF Core). It stores players,
spells, decks and matches, matchmakes, validates plans
(`Matches/PlanValidator.cs`) and relays them. It never simulates the world.

## Tests

`tests/`, Catch2. `whas_tests` runs everything that needs no server;
`dotnet test` in `server/` runs the server's (needs Docker for Postgres).
Golden spell vectors are regenerated with `whas_tests "[.generate]"`.
