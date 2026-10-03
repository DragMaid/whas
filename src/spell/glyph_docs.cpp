#include "whas/spell/glyph_docs.h"
#include <cstring>

namespace GlyphDocs {

namespace {

struct Entry {
  const char *id;
  Info info;
};

constexpr Entry kEntries[] = {
    // Sigils: what the spell is made of
    {"fire",
     {"Fire", "Makes flame. It burns what it lands on and keeps burning; a "
              "bigger sigil burns hotter. Cooling signs chill it, until it's "
              "only harmless smoke."}},
    {"water",
     {"Water", "Makes water. Puts out burning players and things, and "
               "freezes to ice with cooling signs."}},
    {"earth",
     {"Earth", "Makes earth: heavy and slow, but it hits hard and piles up "
               "into cover. Harden it (strengthening, convergence) and it "
               "lands as rock."}},
    {"light",
     {"Light", "Makes light: weightless and very fast, it breaks nothing. "
               "Every mote bursts into a flash that blinds anyone close, you "
               "included. With guidance and a human sigil it flies at your "
               "opponent."}},
    {"wind",
     {"Wind", "Moves air and whatever it carries, players included. Needs a "
              "pulling sign: it pulls loose things toward you, or pushes "
              "them away when the sign is inverted."}},
    {"wind_underfoot",
     {"Wind Underfoot", "Carries you: launches the caster the way the spell "
                        "points. Bigger sigils throw you further. One per "
                        "pause."}},
    {"guidance",
     {"Guidance", "Steers the spell onto a target named by another sigil in "
                  "the circle: a human sigil chases the nearest enemy, a "
                  "second, smaller element sigil the nearest of that element. "
                  "Bigger: turns harder and looks further."}},
    {"human",
     {"Human", "A target, not a spell. With guidance, the spell chases the "
               "nearest other player."}},
    {"dragon",
     {"Dragon", "Shapes the element into a dragon: a horned head and a long, "
                "weaving body. Needs an element sigil beside it and plenty "
                "of material (collection helps)."}},
    // Signs: how the spell behaves
    {"levitation",
     {"Levitation", "Thrust: points the way the spell flies. More or bigger "
                    "signs fly faster and further; unbalanced ones bend it "
                    "sideways."}},
    {"column",
     {"Column", "Holds the element as a block: a pillar in front of you, or "
                "where it's drawn (Q). Levitation launches the block whole. "
                "Shapes are fitted inside it. Bigger signs hold longer; when "
                "it lets go the material is left to the world. Repetition "
                "keeps it as cast and mends it; without, damage stays."}},
    {"convergence",
     {"Convergence", "Packs the spell tighter: faster and further, denser "
                     "and narrower, with less material, and a little harder "
                     "where it lands. Wind underfoot throws you harder; a "
                     "wind field becomes a narrower, stronger jet."}},
    {"crushing",
     {"Crushing", "Grinds what it hits: earth and rock burst out of the hole "
                  "as flying sand, so it digs. Bigger signs dig wider. "
                  "Inverted (F), it packs sand back into earth."}},
    {"repetition",
     {"Repetition", "Puts what it hits back the way it was: its natural heat "
                    "and hardness, no longer burning."}},
    {"cooling",
     {"Cooling", "Chills the element as it's cast: water freezes to ice, "
                 "fire cools (too far and it's only smoke)."}},
    {"strengthening",
     {"Strengthening", "What lands is harder to break. Earth hardened enough "
                       "becomes rock."}},
    {"collection",
     {"Collection", "Draws matching material from around you into the "
                    "spell, so it carries more than the sigil makes."}},
    {"expansion",
     {"Expansion", "Widens the spell and gives it more material. Inverted "
                   "(F), it shrinks."}},
    {"orb",
     {"Orb", "Gathers the element into a ball that forms just ahead of you "
             "before it flies."}},
    {"pulling",
     {"Pulling", "Moves what's already there instead of making more: toward "
                 "you with the arrow pointing in, away when inverted (F). "
                 "Wind moves everything loose, an element sigil only that "
                 "element."}},
    {"sights_set",
     {"Sights Set", "The fired spell follows your cursor for a while, "
                    "turning slowly. More or bigger signs follow for "
                    "longer."}},
};

} // namespace

Info Get(const std::string &assetId) {
  for (const Entry &e : kEntries)
    if (assetId == e.id)
      return e.info;
  return {nullptr, ""};
}

} // namespace GlyphDocs
