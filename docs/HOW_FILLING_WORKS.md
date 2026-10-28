# How bowl filling works: legacy portioning vs volume fill

**Dynamic Portioning** is the step where the robot adjusts a bowl's portions before it
dispenses. This is a plain-language guide to the two ways it can do that, and what this
simulator does to compare them. No maths background needed; the technical version is in
the main [README](../README.md#dispense-models).

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

## Method 1: Legacy portioning (what runs today)

*In the app: **Dynamic Portioning → Legacy portioning**.*

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

## Method 2: Volume fill (the replacement)

*In the app: **Dynamic Portioning → Volume fill**.*

The same ramp steered by volume instead of weight. It works in the same small
steps (`step_g`) the robot already uses, but it ignores the weight floor entirely
and only asks whether the bowl fits. It recommends the same bowl whether or not a
floor is set:

- **If the bowl fits as written**, it tops everything up, proteins included, a step
  at a time, for as long as the next whole step still fits. It stops one step short
  of the limit, or when everything is at its maximum.
- **If the bowl is over as written**, it steps the bases and toppings *down* by
  their step sizes, a step at a time, until it fits. **Proteins are never stepped
  down**, and nothing goes below a set share of its recipe portion (70% by
  default). If it still doesn't fit at that point, the bowl is flagged.

"Fits" means within a set share of the food room (100% by default; a menu can set its
own, as BIG JERK and ORO do at 87%). The food room is the space under the 32 oz fill
line that's left once the sauce cups are in, so a bowl with two cups fills a little
lower than one with none. Because real
bowls run a little heavier than their targets and vary bowl to bowl, a bowl that
only just fits on paper still overfills fairly often. Lowering the fit target
(say to 90%) leaves headroom; the overfill odds under the bowl show the effect.

---

## Side by side

| | Legacy portioning | Volume fill |
| --- | --- | --- |
| Aims for | A minimum **weight** | As full as fits (a share of the food room) |
| Looks at whether it fits? | No | Yes: that's the whole point |
| How it adjusts | Adds a small step of everything, over and over | The same steps, checked against the bowl before each one |
| Can it make a bowl *smaller*? | No, only bigger | Yes, bases and toppings |
| Protein protected? | No special treatment | Never stepped down |
| Bulky recipes | Can be pushed over the rim | Stepped down to fit |
| Light, dense recipes | Topped up to the weight floor | Topped up while it still fits |
| Needs volume data? | No (that's its weakness) | Yes (that's its strength) |

---

## A worked example: Farmstand Pesto Chopped Salad

As written, the recipe is **254 g**, which the simulator predicts takes about
**21 oz** of space: comfortably inside the bowl.

**Legacy portioning:** the menu's weight floor for this bowl is **365 g**, so the
robot tops everything up until it passes 365 g. That extra ~110 g, much of it greens,
grows the bowl to about **30.5 oz**, past the room left for food. The simulator
puts the chance of an overfilled bowl at roughly **64%**.

**Volume fill:** it tops the recipe up six steps, to **283 g** and about **25 oz**, and
stops there because the next step would no longer fit. The chance of overfilling drops
to about **30%**; lowering the fit target leaves more headroom.

Same recipe, same robot: one method makes it overflow most of the time, the other
stops while it still fits.

*(Numbers from the simulator's feasibility report on 2026-10-28, with Farmstand's last
menu; they will move as more measurements come in.)*

---

## What makes the predictions possible

Both methods are judged by how much **space** the food takes, so the simulator needs
a way to turn grams into millilitres. Here is how it gets there.

### Mass-to-volume curves

Each ingredient was dispensed at several weights and scanned with a depth camera to
measure its volume. That gives a curve: "50 g of romaine takes this much space,
100 g takes this much". The curves are not straight lines: many foods pack down
under their own weight, so the second 50 g takes less extra room than the first.
The curves are fitted together, so an ingredient measured only a few times leans on
similar ingredients (grains on grains, proteins on proteins), and one that hasn't been
measured at all takes its group's typical curve.

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

Real bowls vary. The robot misses its targets by a few grams, the same weight of
lettuce fluffs up differently each time, and every batch of food is a little different
(about ±15% so far). Production always fills from a batch nobody has measured, so the
ranges allow for that. So instead of one prediction, the simulator
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
weight). Lowering it for everyone short-changes those bowls. Volume fill judges
each bowl on its own terms.

**Doesn't volume fill mean customers sometimes get less food?**
Sometimes less *base* or topping, on bulky bowls that would otherwise overflow, and
never below a set share of the recipe portion. Protein is never stepped down.

**What if the volume predictions are wrong?**
That's what the 80% range and the coverage check are for. The more ingredients get
measured, the tighter the ranges get. Unmeasured ingredients are flagged in the app
so it's clear which numbers are estimates.

**Where are these settings in the app?**
- Press **Dynamic Portioning** to open the simulator on the bowl you are building;
  the **Legacy portioning / Volume fill** switch is at its top.
- Portions (start, step and max weight for each ingredient) are edited right in the
  finished-bowl table: click a shaded number and type.
- The weight floor, or Volume fill's fit target and step-down limit, are in the
  left-hand panel under the recipe.
- The bowl, lid and cup sizes, dispense variation, mounding, compression and
  measurement data are in **⚙ Settings**, top right.
- **ⓘ estimates** in the header lists everything the numbers rest on that hasn't
  been measured yet.

---

## Glossary

| Term | Meaning |
| --- | --- |
| **Weight floor** | The minimum weight a bowl must reach under legacy portioning; set per bowl type by the menu |
| **Step** | The small amount added to an ingredient on each top-up pass |
| **Fill line / functional fill** | 32 oz (946 ml): how full a bowl is meant to be; a little below the rim |
| **Food room** | Space left for food once sauce cups (and any tall chunks) are accounted for |
| **Fit target** | How full volume fill may make a bowl, as a share of the food room |
| **Mass-to-volume curve** | Measured relationship between an ingredient's weight and the space it takes |
| **80% range** | Where 8 out of 10 real bowls of this recipe are expected to land |
| **COGS** | Cost of goods sold: what the bowl's ingredients cost |
