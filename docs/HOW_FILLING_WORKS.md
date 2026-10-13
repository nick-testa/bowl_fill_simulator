# How bowl filling works: dynamic portioning vs adaptive fill

A plain-language guide to the two ways the robot can decide how much food goes in a
bowl, and what this simulator does to compare them. No maths background needed; the
technical version is in the main [README](../README.md#two-dispense-models).

---

## The problem in one paragraph

The robot measures food by **weight**: it dispenses grams. What actually decides
whether a bowl can be lidded is **space**: millilitres. Those two don't line up,
because different foods take up very different amounts of room per gram. Shredded
romaine takes roughly **three times** the space of the same weight of rice. So two
bowls of identical weight can be very different sizes: one with room to spare, the
other overflowing the rim. Any filling method has to live with that mismatch; the
two methods below handle it very differently.

---

## Method 1: Dynamic portioning (what runs today)

*In the app: **Dynamic portioning**, at the top of the window.*

### The idea

Every bowl must weigh **at least a minimum amount**, called the **weight floor**, so
customers feel they got a full meal. If the recipe as written comes in under that
weight, the robot tops everything up a little at a time until it clears the floor.

### Step by step

1. **Start from the recipe.** Each ingredient begins at its normal portion (say,
   136 g of rice and 47 g of broccoli).
2. **Look up the floor.** The menu has rules that set the minimum weight depending on
   what's in the bowl: for example 365 g for a bowl with greens, 455 g for a
   grain-only bowl. The first rule that matches the bowl wins.
3. **Weigh it.** If the bowl is already over the floor, stop. Nothing changes.
4. **Add one step of everything.** Otherwise, every ingredient gets one small
   increment (a few grams of base, half a gram of protein, and so on), as long as it
   hasn't reached its own maximum.
5. **Repeat** steps 3 and 4 until the bowl clears the floor, or every ingredient is
   at its maximum and there is nothing left to add.

> **Analogy:** filling a bowl on a kitchen scale, with your eyes closed. You keep
> adding spoonfuls of everything until the scale reads high enough. You never look
> to see whether it still fits under the lid.

### What it gets right

- Simple, predictable, and guarantees customers a minimum weight of food.

### Where it goes wrong

- **It never looks at volume.** If the recipe is mostly light, bulky food (greens,
  slaw), topping it up by weight can push it well past the rim before the scale is
  satisfied.
- **It tops up everything at once**, so the bulky ingredients, which are the ones
  causing trouble, keep growing along with everything else.

### Possible outcomes

| What the app says | What it means |
| --- | --- |
| **Fits** | Reached the weight floor and still fits in the bowl |
| **Saturated** | Every ingredient hit its maximum before reaching the floor; fits, but light |
| **Over while ramping** | Fit as written, but the top-ups pushed it over |
| **Over at start** | Too big even before any top-up |
| **No floor** | No weight rule applies to this bowl, so nothing was topped up |

---

## Method 2: Adaptive fill (the proposed replacement)

*In the app: **Adaptive fill**, at the top of the window.*

### The idea

Instead of aiming for a **weight**, aim for a **fill level**: food filling about
**85% of the food room**. The food room is the space under the 32 oz fill line
that's left once the sauce cups are in, so a bowl with two cups aims a little lower
than one with none. Then adjust the recipe up or down, within limits, to land there.

### Step by step

1. **Predict how much space the recipe as written will take.** The simulator does
   this using measured *mass-to-volume curves* for each ingredient (see "What makes
   the predictions possible" below).
2. **If it already looks right, leave it alone.** If the food comes in between
   **75% and 85%** of the food room, nothing is adjusted. Most bowls should land here.
3. **Otherwise, turn one dial.** There is a single "more / less" dial that adjusts
   *every* ingredient at the same time, but each by a different amount:

   | Ingredient type | Allowed to move | Why |
   | --- | --- | --- |
   | Base (rice, greens) | up to ±20% | Cheap and bulky: the best lever for volume |
   | Protein | up to ±5% | What customers notice and pay for, so protected |
   | Topping | up to ±10% | In between |

   Turning the dial halfway means every ingredient moves halfway through *its own*
   allowance: the base moves 10%, the protein 2.5%, the toppings 5%. The base always
   moves four times as far as the protein, and toppings twice as far.
4. **Set the dial so the bowl lands on 85%.** Because turning the dial up always
   makes the bowl bigger, there is exactly one right setting, and it can be found
   quickly and reliably.
5. **If the dial runs out**, flag the bowl rather than break the rules:
   - **Underfilled:** even at full "more", it can't reach 85%. It goes out a little
     light, and is flagged.
   - **Still over at max reduction:** even at full "less", it's too big. This means
     the recipe itself needs changing, not the robot.

   When in doubt it errs toward **underfilling**: a slightly light bowl is a better
   failure than one that won't close.

> **Analogy:** a single volume knob on a stereo with three speakers wired at
> different strengths. One turn of the knob changes all three, but the bass (the
> base) moves most and the vocals (the protein) barely move.

### Cost

Each bowl's ingredient cost is **reported** next to the menu price, against a target
of 23% food cost. It is **not** used to decide the portions. The aim is to see the
cost impact, not to optimise for it. *(The ingredient prices are placeholders for
now.)*

### Possible outcomes

| What the app says | What it means |
| --- | --- |
| **No adjustment** | Already 75–85% of the food room; dispensed exactly as written |
| **Adjusted** | Dialled up or down onto the 85% target |
| **Underfilled** | Couldn't get up to 85% within the allowed limits |
| **Still over at max reduction** | Too big even at the smallest allowed portions |

---

## Side by side

| | Dynamic portioning | Adaptive fill |
| --- | --- | --- |
| Aims for | A minimum **weight** | A **fill level** (85% of the food room) |
| Looks at whether it fits? | No | Yes: that's the whole point |
| How it adjusts | Adds a small step of everything, over and over | Turns one dial, once |
| Can it make a bowl *smaller*? | No, only bigger | Yes |
| Who moves most | Whatever has the biggest step size | The base, by design |
| Protein protected? | No special treatment | Yes, it moves least (±5%) |
| Bulky recipes | Can be pushed over the rim | Pulled back to fit |
| Light, dense recipes | Topped up to the weight floor | Topped up toward 85%, within limits |
| Needs volume data? | No (that's its weakness) | Yes (that's its strength) |

---

## A worked example: Farmstand Pesto Chopped Salad

As written, the recipe is **326 g**, which the simulator predicts takes about
**25 oz** of space: comfortably inside the bowl.

**Dynamic portioning:** the menu's weight floor for this bowl is **365 g**, so the
robot tops everything up until it passes 365 g. That extra ~40 g, much of it greens,
grows the bowl to about **30.6 oz**, past the room left for food. The simulator
puts the chance of an overfilled bowl at roughly **50/50**.

**Adaptive fill:** it predicts the recipe as written fills about **82%** of the food
room, inside the 75–85% "leave it alone" range, so it dispenses the recipe unchanged. The
chance of overfilling drops to about **7%**.

Same recipe, same robot: one method makes it overflow half the time, the other
doesn't touch it.

*(Numbers from the simulator's feasibility report on 2026-10-12; they will move as
more measurements come in.)*

---

## What makes the predictions possible

Both methods are judged by how much **space** the food takes, so the simulator needs
a way to turn grams into millilitres. Here is how it gets there.

### Mass-to-volume curves

Each ingredient was dispensed at several weights and scanned with a depth camera to
measure its volume. That gives a curve: "50 g of romaine takes this much space,
100 g takes this much". The curves are not straight lines: many foods pack down
under their own weight, so the second 50 g takes less extra room than the first.
Ingredients that haven't been measured yet borrow from a close relative (e.g. one
rice for another) or from the average of their category (e.g. all measured
proteins).

### Bowl, lid and sauce cups

The real bowl, lid and sauce cups were measured. The bowl holds about **1058 ml**
to the rim; the "32 oz" everyone talks about is a **fill line** about 4 mm below
the rim. Sauce cups sit in the food and take up roughly 45–70 ml each depending on
how deep they are pressed. The app takes all of this off the room available for
food.

### Mounding

Food dropped at the centre of the bowl piles up instead of levelling out, so a bowl
can stand over the rim even when there's plenty of room by volume. Tick **Show
mounding** to draw each ingredient as a pile with sloping sides and see how high the
peak stands relative to the lid. The lidder presses the mound down to seal, so
mounding doesn't change whether a bowl *fits* (that's still a question of volume),
but it shows how much flattening the lidder has to do. Each ingredient piles on top
of the ones below it at its own steepness (chunky beef stands steeper than couscous),
set per ingredient under *Settings → Ingredients & menu*; anything left on *Default*
uses the slope on the *Mounding & chunks* page. Chunky pieces that roll off a mound (beef
cubes on couscous) can be given a *roll-off* angle in the same place: they roll down
anything steeper than it and ring the foot of the mound instead of piling on top. The slopes are placeholders until they're measured from
side-on photos.

### Ranges, not single numbers

Real bowls vary. The robot misses its targets by a few grams, and the same weight of
lettuce fluffs up differently each time. So instead of one prediction, the simulator
gives:

- the **typical bowl** (the median),
- the range that **80% of real bowls** should land in, and
- the **chance of overfilling** the food room, and of going over the rim.

That's the shaded band and the "80%" bracket on the bowl diagram, and the
percentages above it.

These ranges are checked against real scanned bowls: currently about 84% of
measured mixed bowls land inside their predicted 80% range, which is about right.

---

## Common questions

**Why not just lower the weight floor?**
The floor protects customers on dense bowls (rice-heavy bowls are small for their
weight). Lowering it for everyone short-changes those bowls. Adaptive fill solves
each bowl on its own terms.

**Doesn't adaptive fill mean customers sometimes get less food?**
Sometimes less *base*, mostly on bulky bowls that would otherwise overflow. Protein
barely moves (±5% at most), and many bowls aren't touched at all.

**What if the volume predictions are wrong?**
That's what the 80% range and the coverage check are for. The more ingredients get
measured, the tighter the ranges get. Unmeasured ingredients are flagged in the app
so it's clear which numbers are estimates.

**Where are these settings in the app?**
- The **Dynamic portioning / Adaptive fill** switch is at the top of the window.
- Portions (start, step and max weight for each ingredient) are edited right in the
  finished-bowl table: click a shaded number and type.
- The weight floor, or Adaptive fill's target, "leave alone" threshold and ±%
  allowances, are in the left-hand panel under the recipe.
- The bowl, lid and cup sizes, dispense variation, mounding, compression and
  measurement data are in **⚙ Settings**, top right.
- **Saved scenario** (under the recipe) keeps a named set-up of a bowl, with its
  ingredients and edits, in `data/scenarios`; **Export spec…** on the finished bowl
  saves a one-page PDF of it for hand-off.
- **ⓘ estimates** in the header lists everything the numbers rest on that hasn't
  been measured yet.

---

## Glossary

| Term | Meaning |
| --- | --- |
| **Weight floor** | The minimum weight a bowl must reach under dynamic portioning; set per bowl type by the menu |
| **Step** | The small amount added to an ingredient on each top-up pass |
| **Fill line / functional fill** | 32 oz (946 ml): how full a bowl is meant to be; a little below the rim |
| **Food room** | Space left for food once sauce cups (and any tall chunks) are accounted for |
| **Tolerance / allowance** | How far adaptive fill may move an ingredient from its recipe portion |
| **Mass-to-volume curve** | Measured relationship between an ingredient's weight and the space it takes |
| **80% range** | Where 8 out of 10 real bowls of this recipe are expected to land |
| **COGS** | Cost of goods sold: ingredient cost as a share of the menu price |
