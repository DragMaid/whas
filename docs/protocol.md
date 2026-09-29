# Multiplayer protocol

whas plays online as **turn-level lockstep**. Each client runs the full
simulation itself. Once per turn both players' plans (180 ticks of input and
casts) go through the server, and both clients simulate the same turn from
the same state. The server never simulates. It pairs players, computes spell
stats from glyphs, validates plans, relays them and compares the state hashes
the clients report.

- Transport: one WebSocket per client at `ws://<host>:8080/ws`, text frames,
  one JSON object per frame with a `"type"`.
- Protocol version: `1` (sent in `hello`). Ruleset version: `1`.
- 64-bit values (seeds, state hashes) are **decimal strings**.

## Why it stays in sync

| Hazard | Fix |
|---|---|
| Thread scheduling | Each chunk draws from its own `DetRng(seed, frame, chunk)`. Particles spawned by workers are queued and added in chunk order. |
| `std::rand`, `std::shuffle`, distributions | Not used. `DetRng` is the only randomness. |
| Float contraction / fast-math | `-ffp-contract=off -fno-fast-math` on whas and Box2D |
| Different builds | `hello.buildId` (git hash, compiler, CPU). Matchmaking only pairs equal builds. |
| Spell stats computed in C# vs C++ | The server sends quantized integer stats. Clients simulate on those numbers and never re-evaluate. |
| Aims | Sent as `int16` pairs scaled by 16383 |
| Cast time | Integer formula `max(1, (180 + 3*particles + 5) / 10)` on both sides. A layered spell takes its slowest part, plus half the other parts rounded up, plus `8 + 2*(parts-1)`. |
| Tuning sliders | Online matches use the default config (ruleset 1) |

`tests/fixtures/spells.json` holds 686 golden spells (evaluator version 5).
They cover plain spells, modifier signs, pulling fields, light, guidance, the
dragon sigil and layered spells.
The C++ and C# evaluators must produce exactly the listed quantized stats
(`whas_tests`, `dotnet test`).

## Messages

### Session

| Client → server | Server → client |
|---|---|
| `hello {buildId, token?, protocol}` | `welcome {playerId, token?, protocol, runningMatch?}`. `token` is only present for a new guest; keep it in `data/guest.json`. `runningMatch` means you can `rejoin`. |

### Library

| Client → server | Server → client |
|---|---|
| `uploadSpell {ref, name, glyphs[], components[]?}` | `spellAccepted {ref, spellId, stats}` / `spellRejected {ref, reason}` |
| `listSpells {}` | `spells {spells: SpellCard[]}` |
| `upsertDeck {ref, deckId?, name, spellIds[6]}` (0 = empty) | `deckAccepted {ref, deckId}` / `deckRejected {ref, reason}` |
| `deleteDeck {deckId}`, `listDecks {}` | `decks {decks[]}` |

A glyph has the same shape as the spell files:
`{assetId, kind: "sign"|"sigil", x, y, scale, rotation, inverted?}`.
`inverted` is only sent when it is true, and only `crushing`, `expansion`
and `pulling` may set it.

- Signs: `column` (thrust), plus the modifiers `convergence`, `crushing`,
  `repetition`, `cooling`, `strengthening`, `collection`, `expansion`,
  `orb`, `pulling` and `sights_set`.
- Sigils: the elements (`fire`, `water`, `earth`, `light`), `wind` (only
  with a pulling sign: a field), `wind_underfoot` (flight), plus:
  - `dragon`, a shape sigil that needs an element sigil beside it;
  - `guidance`, which needs a target beside it: `human` (the nearest
    enemy) or a second, smaller element sigil (the nearest cells of that
    element). With two element sigils the bigger one is fired.
- Evaluator version 5 renamed the sigils: stored spells from before it have
  `wind` for today's `wind_underfoot` and `gust` for today's `wind`. The
  server renames them when it re-evaluates a stored spell; client spell
  files are marked `"format": 2` from then on.
- Uploads are rejected unless the evaluator makes a valid spell of them.

A layered spell also has `components`: 1 to 5 entries of
`{source, x, y, scale (0.2-0.7), rotation, glyphs[]}`.

- Each entry is a plain spell.
- The top-level `glyphs` are the outer ring and must all be signs.
- Plain spells leave out `components`, which keeps their upload hash
  unchanged.

A `SpellCard` is `{id, name, glyphs, stats, components?}`, where `stats` is
the quantized `SpellQuant::Stats`:

- `speed`, `range`, `density`, `power`, `diameter`, `temperature`,
  `launchSpeed`, `force`, `duration`, `imbalance`, `temperatureDelta`,
  `hardnessScale`, `crush`, `restore`, `collectRadius`, `pull`,
  `flashRadius`, `flashTime`, `homeTurnRate`, `homeRadius`, `steerTime` and
  `steerRate` are in units of 1/1024. Turn rates are radians per second.
- `offset` is in radians, in units of 1/65536.
- `particleCount` and `collectMax` are plain counts.
- `kind` is 0 (none), 1 (element), 2 (flight), 3 (field: a pulling sign,
  `pull` > 0 pulls toward the caster and < 0 pushes; `element` 0 moves
  everything) or 4 (layered).
- `shape` is 0 (stream), 1 (orb) or 2 (dragon).
- `homeTarget` is 0 (none), 1 (the nearest enemy) or 2 (the nearest cells of
  `homeElement`).
- A missing modifier field means its default: 0, or 1024 for
  `hardnessScale`. The C++ side leaves out default modifier fields; the
  server always writes them.
- A layered spell has `kind` 4 and a `parts` array. Each entry is the full
  stats of one embedded spell, with the outer ring already applied. All the
  parts fire on the same tick.

### Matchmaking

| Client → server | Server → client |
|---|---|
| `queue {}` / `cancelQueue {}` | `queued` / `queueCancelled` |
| `createLobby {}` | `lobbyCreated {code}` (6 characters) |
| `joinLobby {code}` | `matchFound` or `error` |
| | `matchFound {matchId, seed, slot, rulesetVersion, mode, deckDeadlineMs}` |

### Match

1. **Decks.** The client sends `matchDecks {deckIds[3]}`, one deck per round.
   The server replies `decksLocked {rounds: SpellCard?[6][3]}` or
   `decksRejected {reason}`.
2. **Each round.** The server sends `roundStart {round, decks: [slot0Cards, slot1Cards], roundsWon}`.
   Both players see both decks for that round.
3. **Each turn.**
   1. The server sends `turnStart {round, turn, deadlineMs}`. The plan
      deadline is 30 s.
   2. The client sends `commit {round, turn, hash}`, where
      `hash = sha256_hex(plan + nonce)`.
   3. The server sends `opponentCommitted` when the other player commits, then
      `bothCommitted {round, turn, deadlineMs}`.
   4. The client sends `reveal {round, turn, plan, nonce}`. `plan` is the exact
      committed string.
   5. The server validates both plans, then sends
      `turnPlans {round, turn, plans[2], substituted[2]}`. It sends
      `planRejected {reason}` to a player whose plan was replaced with
      standing still.
   6. Both clients run `Match::ExecuteTurn` with `plans` and reply
      `stateHash {round, turn, hash, winner}`. `winner` is -1 while both
      players stand, otherwise 0, 1, or 2 for a double KO.
4. **If the hashes differ.** The server sends `desync {round, turn, referenceSlot}`.
   The reference client sends `snapshot {round, turn, data}`, and the server
   relays it to the other client as `snapshot {round, turn, data, hash}`. The
   reference client's result stands, and the other player is recorded in
   `DesyncReports` as the suspect.
5. `roundEnd {round, winner, roundsWon}`, and finally
   `matchEnd {winner, reason, status, roundsWon}`.

### Disconnects

- **Leaving on purpose.** `leave {}` forfeits.
- **Dropping.** The opponent receives `opponentDisconnected {graceSec: 60}`.
  To come back, reconnect, send `hello` with the same token, then
  `rejoin {matchId}`. The server replies:

  ```
  catchUp {seed, slot, decks, yourDecks, turns[{round, turn, plans}], roundsWon, current {round, turn, phase, committed, deadlineMs}}
  ```

  The client re-simulates from the seed through every turn and carries on.
  A player who doesn't return within the grace period forfeits.

### Plan format

This is `src/net/plan_codec.cpp`. Consecutive steps with the same input are
collapsed into runs, and casts carry the server spell id:

```json
{"v":1,"runs":[{"n":30,"in":2},{"n":1,"in":0,"casts":[{"id":7,"ax":16383,"ay":0}]},{"n":29,"in":0,"c":[1280,400]}]}
```

`in` is a bit field: 1 = left, 2 = right, 4 = jump. `c` is where the
player's cursor was, in 1/8 cells, for sights set spells (two int16s); a
run without it keeps the last one. The server rejects a plan
(`server/Whas.Server/Matches/PlanValidator.cs`) if any of these hold:

- it runs past 180 ticks,
- it moves while channelling,
- its casts would finish after the turn ends,
- it casts a spell that isn't in that round's deck, or an invalid one,
- an aim isn't a unit vector (±2%),
- it has more than one wind underfoot cast in a pause,
- a cursor isn't two int16s.

## REST

Send `Authorization: Bearer <guest token>` with every request.

- `GET /api/players/me` returns `{playerId, wins, losses}`.
- `GET /api/players/me/matches` returns your last 50 matches.
- `GET /api/matches/{id}/replay` returns the seed, build, both players' round
  decks and every turn's plans and hashes. That is enough to re-simulate the
  match.
- `GET /api/spells/mine`, `GET /api/decks/mine`
- `GET /health`

## Running it

```sh
docker compose up -d                        # Postgres + server on :8080
cd server/Whas.Server && dotnet run          # or: the server alone on :8080 (needs the db container)
./build/whasg                               # Play (M) > Connect > Quick match
./build/whasg                               # a second copy is automatically a different guest
WHAS_SERVER=ws://localhost:8080/ws ./build/whas_tests "[.bot]"   # sparring bot
```

Tests:

```sh
./build/whas_tests                    # determinism, lockstep, snapshots, burning, goldens, replay
cd server && dotnet test              # evaluator goldens, plan validation, full matches (needs Docker)
WHAS_SERVER=ws://localhost:8080/ws ./build/whas_tests "[.e2e]"   # two bots, one real match
./build/whasg --replay match.json --verify   # re-simulate a replay from GET /api/matches/{id}/replay
```

`tests/replays/bot_match.json` is a real recorded match. If a deliberate
simulation change stops it from reproducing, record a new one with the
`[.e2e]` test and save its replay there.
