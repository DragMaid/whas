# Spells

A spell is a circle of glyphs: **sigils** say what it's made of, **signs**
say how it behaves. The editor (`E`) shows what a spell does as you draw;
`spell/glyph_docs.cpp` has the in-game descriptions.

## Sigils

| Sigil | Makes |
|---|---|
| `fire`, `water`, `earth`, `light` | That element, fired as a stream |
| `wind_underfoot` | Flight: launches the caster |
| `wind` | Nothing alone; with a pulling sign, a wind field |
| `dragon` | A shape: the element beside it as a dragon |
| `guidance` + `human` / a second element | The spell chases the nearest enemy, or the nearest cells of that element |

## Signs

| Sign | Effect |
|---|---|
| `levitation` | Thrust: points and speeds the spell. Unbalanced signs bend it. (Before evaluator version 7 this was called `column`.) |
| `column` | Holds the element as a block that rises out of its base; with crushing, a drill. Points like levitation. See below. |
| `convergence` | Faster, denser, narrower |
| `crushing` | A digging tool: the spell leaves none of its own element. Rock and earth it hits burst into sand thrown back the way the spell came, out of the hole (sand it runs into is thrown back too). Inverted: packs sand it hits into earth. |
| `repetition` | Puts what it hits back to its natural state. On a column: keeps the block as cast and mends it. |
| `cooling` | Chills: water to ice, fire down to smoke |
| `strengthening` | Lands harder; earth becomes rock |
| `collection` | Draws matching material from around the caster |
| `expansion` | Wider, more material. Inverted: smaller. |
| `orb` | A ball formed ahead of the caster |
| `pulling` | A field that moves existing material toward (or, inverted, away from) the caster |
| `sights_set` | The spell follows the cursor for a while |

### Column

A column spell doesn't stream. It lays its element out as a rectangle
along the aim, `holdLength` cells long and `holdWidth` wide (the beam's
width), and holds it for `holdTime` seconds (more and bigger column signs
hold longer).

- **Direction**: column signs point like levitation signs. Their net vector
  turns the block off its base direction, up to 60 degrees, the same way
  levitation turns a flight.
- **No levitation**: the block rises out of its base at `holdRise` cells/s:
  `(20 + 15 x column signs) x sqrt(60 / area)`, at least 4. The base is
  where the aim line leaves the caster's body, or, cast from a surface
  (right click), the surface itself, rising straight out of it. Air and
  gases are taken, loose cells (sand, water) are carried off ahead of the
  front, and solid ground stops that lane. A character over it is lifted
  (the unstuck rule moves it onto the new top). Its hold time starts once
  it's fully risen.
- **Drill (crushing, no levitation)**: rises the same way but builds
  nothing, so no material limits it: it's 16 cells deep per column sign
  (scale counts) and at least 10 wide, room for its caster to drop in.
  Every earth and rock cell in its risen part, rigid rock bodies included,
  is ground to sand thrown back toward its base, loose sand and water in
  it are thrown out the same way, and it keeps at it until its time runs
  out. Aimed at your feet it digs the ground out from under you. Drawn on
  a surface (right click) it bores into it. Inverted, it packs sand into
  earth.
- While a column rises or holds, an outline shows the block (faint where
  it's still to rise) and a ring at its base drains as its time runs out:
  gold for a block, red for a drill.
- **With levitation**: the block flies as one piece for the spell's range
  and is held where it lands. Whatever it lost on the way stays lost.
- **Shapes** (orb, dragon) are scaled down to fit the block; whatever
  doesn't fit is left out.
- **While held**, its cells are flagged `CELL_HELD`: they don't update, can't
  be pushed aside and don't become rigid bodies. With repetition the block
  is reset every tick (heat, hardness, not burning) and up to
  `3 x repetition` holes are mended per tick. Without it, damage stays.
- **When it lets go**, the flags are cleared and the material is ordinary
  again: water falls, sand piles up, rock settles. Nothing vanishes.

Code: `FormBlock`, `RiseBlock`, `Drill`, `LandBlock`, `HoldBlock` and
`ReleaseBlock` in `spell/spell_system.cpp`; the indicator is
`UI::DrawActiveColumns`.

## Placed casts (right click)

Left click casts from the body. Right click draws the spell on the ground
or wall nearest the cursor (within `Placement::REACH`, 48 cells, of the
caster; a ring marks the spot) and fires it from there, the way you're
aiming, or straight out of the surface when that way leads into it. The
spot travels in the plan as an offset from the caster (`px`, `py` in 1/8
cells), so online peers agree. Wind underfoot always leaves the body. A standing
column ignores the aim and rises straight out of the surface (a drill
bores in); its aim is set when the cast is planned, so it travels the same
way.

## Status effects

| Effect | Caused by | Does | Ends |
|---|---|---|---|
| Burning | Fire cells, burning cells, loose fire, fire bolts passing through | Damage per stack per second, up to 5 stacks | Water; cools by 2 stacks a turn |
| Wet paper | Water cells, water particles, water bolts | Only wind underfoot can be cast | 3 s (1 s while flying); casting a flight; fire |

Fire bolts aren't stopped by a body: they burn it and fly on (each
particle hits a body once). Both effects are part of the match state and
hash.

## How stats travel

1. `SpellSystem::Evaluate` (C++) or `SpellEvaluator.Evaluate` (C#) turn
   glyphs into float stats.
2. `SpellQuant::Quantize` rounds them to integers. That's what the server
   stores and hands out, and what clients simulate with.
3. Any change to the evaluator bumps `SpellQuant::EVALUATOR_VERSION` and
   `SpellEvaluator.Version`, and the golden vectors are regenerated
   (`whas_tests "[.generate]"`). The server re-evaluates stored spells when
   it reads them.

Renamed glyph ids are migrated in three places: client spell files (by
their `"format"`, `SpellJson::MigrateLegacyIds`), stored server spells (an
EF data migration plus `SpellService.MigrateLegacyIds` when re-evaluating),
and campaign rooms (mage spells, through the same client function).
