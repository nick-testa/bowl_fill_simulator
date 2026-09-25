# Bowl Fill Simulator

A Qt6 desktop port of the HTML bowl-fill simulator. It models what the dynamic
portion algorithm does to a bowl — which optimises on **mass** — against the limit
that actually makes bowls fail at the lidder, which is **volume**.

Menu settings are read from the bridge-service dumps in `menus/` at startup, so
editing a menu and relaunching is enough to see the effect. No regeneration step.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/bowl-fill-simulator
```

Needs Qt 6.3 or newer (Core + Widgets) and a C++20 compiler. Nothing else — JSON
parsing is `QJsonDocument`, and the curve fitting is a few dozen lines of OLS.

### If the text looks soft

Configure from a shell where lab37's `env.sh` has been sourced and CMake will find
that tree's vendored **Qt 6.3**, which ships **no Wayland platform plugin**. On a
Wayland session the app then falls back to XWayland, renders at 1x and lets the
compositor upscale it — which is what blurry text looks like on a HiDPI display.

The build prefers the system Qt for this reason and prints which one it picked:

```
-- bowl-fill-simulator: Qt 6.11.2 from /usr/lib
```

If that line names a path under `.toolchain/`, configure a fresh build directory
with `-DCMAKE_PREFIX_PATH=/usr`. It also warns at configure time when the Qt it
found has no Wayland plugin and `WAYLAND_DISPLAY` is set.

## Layout

```
menus/     bridge-service menu dumps, one per brand, read at startup
data/      mass_to_volume.csv — the 36 measured bowls that seed the curves
photos/    reference photographs of known masses in a bowl
src/core/  simulation, curve fitting and menu parsing (no widgets)
src/ui/    Qt widgets, charts drawn with QPainter
tests/     verify.cc, checked against the HTML simulator's output
```

`src/core` links only `Qt6::Core`, so the model is usable from a headless tool.

## Editing the menus

`menus/*.json` are the raw `lab37-bridge-service-tool --get-menu-mappings` dumps,
two-line header and all. The app re-reads them every launch, so to try a config
change — a different `step_increment_g`, a lower `max_dispense_weight_g`, a new
floor — edit the JSON and restart.

What it reads out of each dump:

| from | used for |
| --- | --- |
| `dynamic_portion_increases[].ingredients[]` | step and max per ingredient |
| `dynamic_portion_increases[].apply_for[]` | the weight floors and their filters |
| `ingredients[]`, `recipes[]` | configured portion weights and minimums |
| `product_templates[]` | the preconfigured bowls in the recipe picker |
| `product_rules[]` | whether the 50:50 base split covers this brand |

Portion weights are resolved from the menu item ids, which come in three shapes and
put the ingredient name in a different place in each. Standalone ids are treated as
build-your-own portions; `mb-pt:` template-scoped ids belong to preconfigured bowls
and are kept separate, because a plated salad is often at or above the cap and
therefore cannot ramp at all. Where an ingredient has no configured weight anywhere
it borrows the modal weight of its class within its family (greens never borrow from
grains), and the UI marks that row so an inferred number is never mistaken for
configuration.

## Adding mass-to-volume measurements

**Upload CSV…** in the left column, with the **?** beside it for the format. Rows
are added to the bundled 36 and the affected curves are refitted in place; **Reset**
discards uploads and returns to the bundled set.

```csv
method,ingredient,g,oz
robot,romaine,86,20
robot,kale,73,25
hand,mexican rice,150,11
```

- `ingredient`, `g`, `oz` are required, in any order and any case.
  `mass`/`grams`/`mass_g` and `volume`/`ounces`/`volume_oz`/`fl_oz` also work.
- `method` is optional and defaults to `robot`.
- Names match loosely, so `kale`, `Kale` and `Massaged Kale` land in one bucket.
- An ingredient not seen before gets a curve of its own once it has two points at
  different masses. One point is not a curve, and the app says so rather than
  fitting a line through it.

## The model

**Mass to volume** is `oz = K · g^p`, fitted by OLS on log-log. A straight line fits
the measured bowls about as well but only by carrying an intercept — 7.3 oz of kale
at zero grams — so the power law is the honest form. `p < 1` is compaction: each
added gram buys less volume than the one before it. Romaine sits at 0.94, nearly
proportional; kale and rice at about 0.72.

**The ramp** mirrors `maybe_apply_dynamic_portion_alogithm`: every ingredient still
under its `max_dispense_weight_g` takes one `step_increment_g` per pass, stopping
when nothing moved or the total is strictly greater than the floor.

**The floor** mirrors `find_best_dynamic_portion_filter`: `must_not_have` vetoes
outright, an empty `must_have` always matches, `ExactlyOne` needs exactly one hit,
and the first matching filter wins.

**The 50:50 split** mirrors `apply_proportional_reduction_to_product`: halve each
base, then floor at `per_portion_minimum_weight_g`.

**Compression** is an extension, not a measurement. A bed of mass `g` carries a mean
internal load of about `g/2` and specific volume goes as `load^(p-1)`; anchoring so
that zero surcharge reproduces `K·g^p` exactly gives
`V = g · K · 2^(p-1) · (g/2 + φ·M)^(p-1)`, where `M` is whatever is dispensed on top.
No loaded bowls were ever measured, so this is an extrapolation from the unloaded
curves' own curvature, and load transfer 1.0 is its upper bound.

Robot and hand measurements are fitted separately. Hand-filled bowls hold about 5.8%
more volume at equal mass (p = 0.002), concentrated in the greens — kale +13%,
romaine +6%, rice unchanged — so pooling is available but a poor idea for kale.

## Tests

```bash
./build/bowlfill-verify     # or: ctest --test-dir build
```

70 checks, asserted against the HTML simulator this was ported from: the fitted
curves, menu parsing including the template/standalone split, floor matching, six
end-to-end recipes, the compression model's zero-load identity, and CSV ingest
including malformed input.

## Command line

```
--assets <dir>      where menus/, data/ and photos/ live
--select "Brand|Recipe"
--size WxH
--scale n           render at this device pixel ratio
--dark
--screenshot <file> render to PNG and exit
```

`--screenshot` works under `QT_QPA_PLATFORM=offscreen`, which is how the layout is
checked without a display server. Pair it with `--scale 2` for a sharp PNG — an
offscreen surface is 1x otherwise, and small chart labels suffer for it.

## Known limits

- Proteins and toppings have **no measurements**. They use a flat oz/100 g, editable
  per ingredient, and are treated as incompressible.
- White Rice and Brown Rice and Lentils have no curve of their own and borrow
  Mexican Rice's, flagged in the UI as a proxy.
- The bundled menu dumps are a 2026-09-17 snapshot and already trail production:
  romaine's cap is now 80 g and its minimum 15 g, and Cowgirl's kale cap is 80 g.
  Re-pull before drawing conclusions from the exact numbers.
