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
| Cast time | Integer formula `max(1, (180 + 3*particles + 5) / 10)` on both sides |
| Tuning sliders | Online matches use the default config (ruleset 1) |

`tests/fixtures/spells.json` holds 326 golden spells. The C++ and C#
evaluators must produce exactly the listed quantized stats (`whas_tests`,
`dotnet test`).

## Messages

### Session

| Client → server | Server → client |
|---|---|
| `hello {buildId, token?, protocol}` | `welcome {playerId, token?, protocol, runningMatch?}`. `token` is only present for a new guest; keep it in `data/guest.json`. `runningMatch` means you can `rejoin`. |

### Library

| Client → server | Server → client |
|---|---|
| `uploadSpell {ref, name, glyphs[]}` | `spellAccepted {ref, spellId, stats}` / `spellRejected {ref, reason}` |
| `listSpells {}` | `spells {spells: SpellCard[]}` |
| `upsertDeck {ref, deckId?, name, spellIds[6]}` (0 = empty) | `deckAccepted {ref, deckId}` / `deckRejected {ref, reason}` |
| `deleteDeck {deckId}`, `listDecks {}` | `decks {decks[]}` |

A glyph has the same shape as the spell files:
`{assetId, kind: "sign"|"sigil", x, y, scale, rotation}`.

A `SpellCard` is `{id, name, glyphs, stats}`, where `stats` is the quantized
`SpellQuant::Stats`:

- `speed`, `range`, `density`, `power`, `diameter`, `temperature`,
  `launchSpeed`, `force`, `duration` and `imbalance` are in units of 1/1024.
- `offset` is in radians, in units of 1/65536.
- `particleCount` is a plain count.

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
{"v":1,"runs":[{"n":30,"in":2},{"n":1,"in":0,"casts":[{"id":7,"ax":16383,"ay":0}]},{"n":29,"in":0}]}
```

`in` is a bit field: 1 = left, 2 = right, 4 = jump. The server rejects a plan
(`server/Whas.Server/Matches/PlanValidator.cs`) if any of these hold:

- it runs past 180 ticks,
- it moves while channelling,
- its casts would finish after the turn ends,
- it casts a spell that isn't in that round's deck, or an invalid one,
- an aim isn't a unit vector (±2%),
- it has more than one wind cast in a pause.

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
docker compose up -d          # Postgres + server on :8080
cd server && dotnet test      # evaluator goldens, plan validation, full matches (needs Docker)
./build/whas_tests            # determinism, lockstep, burning, goldens
```
