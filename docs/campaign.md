# Campaigns

A campaign is a set of rooms on a grid. Each room is one screen of world
with a background, things to use and enemies. You start knowing a few
glyphs and learn more from shrines; you draw spells at workbenches and
carry three of them.

Open it from the menu: **Campaign**, or `--open campaigns`.
`--open campaign:<id>` jumps straight into one (continuing its save).

## Playing

| Key | Action |
|---|---|
| `A` / `D`, `W` or `Space`, `S` | Walk, jump, dive |
| Click | Cast the selected spell |
| `1`-`3` | Select a slot |
| Right click | Cast from the ground or wall nearest the cursor |
| `E` | Use what you're standing at: a workbench, a gate |
| `B` | Backpack: put your spells in the three slots |
| `Esc` | Close a panel, or leave (progress is saved) |

- **Rooms**: walk, fly or fall off an edge to enter the room on that side.
  An edge with no room beyond it is a wall. A room is rebuilt as it was
  made every time you enter it, and its enemies come back (the previous
  batch is removed first).
- **Windowway gates**: walking into one opens it and makes it your
  respawn point. `E` at any open gate shows the rooms you've been through;
  click a room with a lit gate to travel there.
- **Shrines** teach their sigil or sign the first time you touch them.
- **Workbenches** (`E`) open the spell editor with only the glyphs you
  know. What you save goes into this campaign's backpack, not your main
  library.
- **Casting** works like real-time duels: a spell can't be cast again until
  its cast time has passed. Wet paper, burning and the rest apply.
- **People** (NPCs) stand where they were placed; `E` talks to them, and
  `1`-`3` or a click picks a reply. A conversation can teach a glyph.
  Spells, enemies and guidance pass them by.
- **Sealed edges**: a room can seal some of its edges with a glowing
  barrier until its conditions hold (listed top right). Once met, they stay
  met in your save.
- **Dying** puts you back at the last gate you touched (or the start), with
  everything you've learned.

The world pauses while the backpack, the gate map or the workbench is open.

## Enemies

`campaign/enemies.cpp`. Each enemy has a `Character` body, so spells, fire,
water, knockback and wet paper affect it like the player. Enemy spells
never hit other enemies (hurtbox teams), but everything else in the world
does.

| Enemy | Behaviour |
|---|---|
| Mage | Keeps its distance (closes in past 80 cells, backs off inside 45), casts a random one of its spells at the player when it can see them, leading moving targets a little |
| Undead | Runs at the player, jumps walls and ledges, and on contact deals its damage and knocks the player away |
| Flyer | Flies straight at the player when the way is clear; otherwise follows an A* path on a 4-cell grid, re-planned every 0.4 s. If it stops making progress it re-plans and jitters loose. |

Health, speed and touch damage are set per enemy in the editor; a mage's
spells are picked from your library and copied into the room.

TODO: enemies and NPCs use the player's sprite, tinted, with a marker (hat, wings,
eyes). Gates and workbenches are drawn shapes. Swap in real art when there
is some (`Enemies::Draw`, `DrawCampaignObject`).

## Editing

From the campaign list: **Edit**, or create a new one by name.

The toolbar along the top holds the tools; the panel on the right has the
rooms map and tabs for what's selected, the room and the campaign.

| Key | Tool |
|---|---|
| `B` | Paint terrain: `1`-`9` elements, `E` eraser, `[` `]` or wheel for size, left paints, right erases, `Ctrl+Z` undoes a stroke |
| `V` | Select: click to select, drag to move, `Delete` removes, right click or `Esc` lets go |
| `T` | Start: where a new game begins |
| `G` / `W` / `H` | Gate / workbench / glyph shrine |
| `N` | Someone to talk to |
| `M` / `U` / `F` | Mage / undead / flyer |
| `Ctrl+S` | Save |

With a placing tool, clicking something already there selects and drags it
instead.

- **Rooms** map: click a room to edit it; `+` beside the selected room adds
  a new one on that side. The room you leave is saved.
- **Selected** tab: an enemy's health, speed, touch damage and tag (a
  mage's spells from your library); a shrine's glyph; an NPC's name, tag
  and dialogue. Dialogue is a list of nodes: node 1 starts the talk, each
  node has a line, up to three replies (each leads to a node or ends the
  talk) and optionally a glyph it teaches.
- **Room** tab:
  - *To leave this room*: tick the edges to seal and add conditions. Every
    condition must hold to open them:
    | Condition | Met when |
    |---|---|
    | Defeat enemies | every enemy with the tag (all of them when empty) is down |
    | Break blocks in a region | that share of the solid cells that start in the region are gone |
    | Fill a region | the region holds that many cells of the element (a rock pushed in, water poured in) |
    | Talk to someone | the NPC with the tag was talked to (up to a node, or at all) |
    | Take the shrine | the room's shrines are taken |
    Regions are drawn with "Draw region": drag a rectangle in the room.
  - *Background*: drop a PNG on the window or type its path. It's scaled
    to the world's size and copied into the campaign folder.
  - *Terrain*: copy a 1v1 map in, or clear to a bare floor.
- **Campaign** tab: the starting kit, the glyphs a new game knows.
- The wave icon lets the world settle (water and sand come to rest).
- **Play this room** saves and starts there with every glyph unlocked,
  without touching the save. `Esc` comes back to the editor.

## Files

```
data/campaigns/<id>/
  campaign.json        name, room positions, start room and spot, starting kit
  rooms/<x>_<y>.json   terrain (a MapDef: run-length encoded cells, world
                       settings), background file, objects, enemies, NPCs
                       and their dialogue, conditions, sealed edges
  backgrounds/*.png    scaled to GRID_W*CELL_SIZE x GRID_H*CELL_SIZE
  backpack/*.json      spells drawn at workbenches (spell file format)
  save.json            glyphs known, rooms visited, gates opened, respawn,
                       shrines taken, slots, rooms cleared, conversations had
```

`save.json` and `backpack/` are per player and ignored by git; the rest is
the campaign itself.

## Loading

Only the room you're in is in the world. When you enter a room, its four
neighbours are read on a worker thread: the room file is parsed and its
background PNG decoded. Crossing an edge then only rebuilds the terrain and
uploads one texture, without waiting on the disk. Rooms further than one
step away are dropped from the cache. Code: `CampaignPlay::Preload`,
`Fetch` and `EnterRoom`.
