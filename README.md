# Witch Hat Atelier Simulator

A falling-sand duel game. You draw spells as circles of sigils and signs, then fight best-of-three rounds. Each round is either planned turns or real time, played solo against a dummy or online through a lockstep server.

- Client: C++20, raylib and ImGui (`src/`, `include/`)
- Server: .NET 10 with Postgres (`server/`)
- Wire protocol: `docs/protocol.md`
- More documentation (architecture, spells, campaigns): [`docs/`](docs/README.md)

## Building the game

You need CMake 4.0 or newer, a C++20 compiler and Ninja (or make). The first configure downloads raylib, ImGui, rlImGui, Box2D, nanosvg, nlohmann/json, IXWebSocket (with OpenSSL) and Catch2.

Run every command from the repository root. The game loads `assets/` and keeps its data in `data/`, both relative to the working directory.

### Optimized build (for playing)

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-release -j
./build-release/whasg
```

### Debug build (for gdb, slow)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/whasg
```

A debug build has no optimisation, so the sand simulation runs about 4x slower. On a typical machine:

| Build | Sandbox frame time |
|---|---|
| `RelWithDebInfo` | about 7 ms |
| `Debug` | about 25 ms |

At 60 fps a frame has 16.7 ms. If the game feels laggy, check the build type first:

```sh
grep CMAKE_BUILD_TYPE build/CMakeCache.txt
```

To switch a build folder that already exists, configure it again with the build type you want:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

### CMake options

| Option | Default | What it does |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `RelWithDebInfo` (when unset) | `Debug`: no optimisation, full debug info. `RelWithDebInfo`: optimised, with symbols. `Release`: optimised, no symbols. |
| `WHAS_BUILD_TESTS` | `ON` | Builds the `whas_tests` target. `-DWHAS_BUILD_TESTS=OFF` skips Catch2 and the tests. |
| `FETCHCONTENT_BASE_DIR` | `<build>/_deps` | Where downloaded dependencies go. Point a second build folder at an existing `_deps` to skip downloading again. |
| `FETCHCONTENT_FULLY_DISCONNECTED` | `OFF` | With `ON`, CMake uses the already downloaded dependencies and doesn't touch the network. |

The simulation is always compiled with `-ffp-contract=off -fno-fast-math` (`/fp:precise` on MSVC). Lockstep peers have to compute bit-identical floats.

Only clients with the same build id can play together. The id is made of the git commit, the compiler and its version, the CPU architecture and the build type, so a Debug and a Release build won't be matched.

### Targets

```sh
cmake --build build --target whasg       # the game only
cmake --build build --target whas_tests  # the tests only
```

## Running the game

```sh
./build/whasg [flags]
```

| Flag | What it does |
|---|---|
| `--server ws://host:8080/ws` | Connect to a server at start-up (`wss://` works too, for TLS tunnels like ngrok) |
| `--identity data/guest-2.json` | Use another guest identity file. Lets two copies on one machine play as different players; a second copy picks a free one on its own. |
| `--open SCREEN` | Start on a screen instead of the menu: `sandbox`, `duel` (solo planned duel), `rts` (solo real-time duel), `spells` (library), `spell-editor`, `maps` (gallery), `map-editor`, `campaigns` (list), `campaign:<id>` (continue that campaign) |
| `--watch replay.json` | Open a saved replay, for example one from `data/replays/` |
| `--screenshot shot.png` | Save the screen after one second and quit (for checking screens without clicking through them) |
| `--replay match.json --verify` | No window: re-simulate a replay and check every turn against the hashes the players reported. Exits 0 when everything matches. |

### Keys

| Key | Action |
|---|---|
| `M` / `Esc` | Menu |
| `E` | Spell editor |
| `F1` | Solo duel or sandbox |
| `F3` | Debug overlay and cell inspector |
| `F4` | Simulation settings |
| `F11` | Borderless window |
| `1`-`6` | Pick a hotbar spell |
| Right click | Cast from the ground or wall nearest the cursor (a ring marks the spot). In the sandbox, Shift + right click moves the dummy. |

**Planned duel**

- `Space`: stop or start time
- Click: cast
- `A`/`D`: walk
- `W`: jump
- `S`: dive
- `Backspace`: undo
- `Enter`: send the plan

**Real-time duel**

- `A`/`D`: walk
- `W` or `Space`: jump
- `S`: dive
- Click: cast the selected spell. A spell can't be cast again until as long as its cast time has passed.

**Campaign**: see [docs/campaign.md](docs/campaign.md) (`B` backpack, `E` use, `1`-`3` slots)

**Map editor** (the campaign editor paints the same way)

- Left click: paint, right click: erase
- `1`-`9`: pick an element (the swatches along the top show their colours), `E`: eraser
- `[` `]` or the mouse wheel: brush size
- `Ctrl+Z`: undo a stroke
- Drag the I and II markers: move the spawns
- `Tab`: hide the panel

### Local data (not in git)

| Path | Contents |
|---|---|
| `data/spells/` | Your spells |
| `data/decks/` | Your decks |
| `data/match_decks.json` | Which deck each round uses |
| `data/maps/` | Your maps and their thumbnails |
| `data/replays/` | Replays of the online matches you finished |
| `data/guest*.json` | Your guest identity per server |
| `data/campaigns/*/save.json`, `backpack/` | Your progress and workbench spells in each campaign (the campaigns themselves can be committed) |

## Tests

```sh
cmake --build build --target whas_tests
./build/whas_tests                         # everything that needs no server
./build/whas_tests "[map]"                 # one tag
./build/whas_tests "rounds rotate*"        # tests whose name matches
./build/whas_tests --list-tags
```

Tags:

- **Always run:** `[arena]`, `[map]`, `[match]`, `[spell]`, `[replay]`, `[lockstep]`, `[net]`, `[codec]`, `[determinism]`, `[snapshot]`, `[burning]`, `[audio]`, `[character]`, `[glyph]`
- **Hidden (only run when named):**
  - `[.e2e]`: two bot clients play a whole match through a real server
  - `[.bot]`: a bot waits in the queue for you to play against
  - `[.bench]`: simulation step timings
  - `[.generate]`: rewrites the golden spell fixtures

The hidden ones that need a server read its address from `WHAS_SERVER`:

```sh
docker compose up -d
WHAS_SERVER=ws://localhost:8080/ws ./build/whas_tests "[.e2e]"
WHAS_SERVER=ws://localhost:8080/ws ./build/whas_tests "[.bot]"
./build/whas_tests "[.bench]"
```

The full suite takes a few minutes in a Debug build and much less in an optimized one.

`tests/replays/bot_match.json` is a real recorded match. When a deliberate simulation change stops it from reproducing, record a new one with the `[.e2e]` test and save its `GET /api/matches/{id}/replay` there.

## The server

```sh
docker compose up -d                       # Postgres and the server on :8080
# or the server on its own (still needs the db container):
docker compose up -d db
cd server/Whas.Server && dotnet run
```

Then point the game at `ws://localhost:8080/ws`: Menu, then Online Duel, then Connect, or start it with `--server`.

Server tests need Docker. Testcontainers starts its own Postgres.

```sh
cd server
dotnet test                                     # all
dotnet test --filter "FullyQualifiedName~Lobby"  # by name
```

Settings live in `server/Whas.Server/appsettings.json`. Any of them can be overridden with an environment variable that uses `__` as the separator:

| Variable | What it sets |
|---|---|
| `ConnectionStrings__Whas` | The Postgres connection |
| `Match__PlanDeadline` | Turn timer (for example `00:00:30`) |
| `Match__DeckDeadline` | Time to lock decks |
| `Match__RevealDeadline` | Time to reveal a committed plan |
| `Match__HashDeadline` | Time to report a state hash |
| `Match__SnapshotDeadline` | Time to send a resync snapshot |
| `Match__RejoinGrace` | How long a dropped player has to come back |
| `Match__MaxTurnsPerRound` | Turns before a planned round is a draw |

After changing the data model, add a migration:

```sh
cd server
dotnet tool restore
dotnet ef migrations add Name --project Whas.Server
```
