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

Needs Qt 6.3 or newer (Core + Widgets) for the app, a C++20 compiler, and
nlohmann/json 3.11 or newer for reading the menus. An installed nlohmann/json is used
when CMake finds one (lab37's toolchain has 3.12); otherwise configure downloads the
3.12.0 release. The curve fitting is a few dozen lines of OLS.

### macOS app for people who don't build from source

`.github/workflows/mac-app.yml` builds a self-contained `Bowl Fill Simulator.app`.
Run it from the Actions tab, or push a `v*` tag to also attach the zip to a release.
The app carries its own copy of `menus/`, `data/` and `photos/`, which it lays out in
`~/Library/Application Support/Lab37/Bowl Fill Simulator` and reads from there, so
only committed files ship. Send `docs/MAC_INSTALL.md` along with the zip.

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
data/      measurements/ — every mass→volume measurement, loaded at startup
           archive/ — measurements kept but not loaded (the original 36-bowl run)
           ingredient_costs.csv — placeholder prices for the COGS readout
           ingredient_aliases.csv, ingredient_pieces.csv — the Ingredients dialog
photos/    reference photographs of known masses in a bowl
src/core/  simulation, the adaptive solver, curve fitting and menu parsing (plain C++, no Qt)
src/ui/    Qt widgets, charts drawn with QPainter
tests/     verify.cc, checked against the HTML simulator's output (plain C++, no Qt)
```

`src/core` is plain C++20 on the standard library, with nlohmann/json kept private
to the menu loader. It has no Qt, so it can be built into lab37 as it is. Strings in
the core are UTF-8 `std::string`; the UI converts at the boundary with the helpers in
`src/ui/qt_text.hh`. The Manual Entry stations are saved in the app's `QSettings`,
so their load and save live in `src/ui/manual_entry_settings.hh`, not in the core.

## Measurements

Every measurement lives as a CSV file in `data/measurements/`, and nothing else
feeds the curves. Each launch, and the **Reload** button in Settings → Measurements, reads every
`.csv` there. Uploading through the app copies the file in, so it persists. To
correct or drop data, edit or delete the file (or rows in it) and press Reload.
To set data aside without deleting it, move the file to `data/archive/`.
`data/measurements/README.md` describes the two file layouts.

The tests do not read this folder: they use their own copy of the original run,
`tests/data/mass_to_volume.csv`, so their expected numbers stay fixed.

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

### Manual Entry

The **Manual Entry** brand, last in the brand list, is built from what is loaded on
the robot rather than from a menu dump. Set it under **Settings → Robot stations**:
19 ordered stations, each holding one ingredient and what it serves as (base,
protein, topping or sauce), plus the sauces served in cups and one weight floor for
every bowl (used by dynamic portioning; Volume fill ignores floors). Pick any name a
brand uses, or type one in.

A known ingredient takes its step, max and portion from a brand that ramps it. A
written-in one borrows the median settings for its kind and shows as an estimate.
The stations are kept in the app's settings (`manual/*`), so `--audit "Manual Entry"`
and `--sweep "Manual Entry"` use the same list.

## Volume per gram (choosing step_g)

**Settings → Volume per gram** lists, for the brand on screen, how many ml a gram of
each ingredient takes and how many grams make an ml. It also shows what the configured
`step_g` adds, and the step that would add a chosen volume ("Volume per step").

Equal gram steps grow a bowl unevenly. With today's Farmstand settings a 3.5 g step adds
about 17 ml of romaine but under 7 ml of rice. The last column gives a volume-proportional
step: every ingredient's step adds the same volume.

Everything is read at the menu portion, where the ramp starts, with nothing pressing on
the food. The curves bend, so a gram is worth a little less further up the ramp. Rows
whose curve is borrowed, composite or flat are estimates, and their numbers will move as
measurements come in.

The same table is available as CSV from the page, or from the command line:

```
./build/bowl-fill-simulator --steps Farmstand --step-ml 10 > farmstand-steps.csv
```

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
- `ml` may be supplied instead of `oz` and is converted on import.
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

94 checks, asserted against the HTML simulator this was ported from: the fitted
curves, menu parsing including the template/standalone split, floor matching, six
end-to-end recipes, the compression model's zero-load identity, and CSV ingest
including metric input and malformed input — plus the Adaptive solver: the 4:1:2
identity, all three Day-in-the-Life scenarios, tolerance and clamp bounds,
monotonicity, and that solving for volume ignores price.

## Dispense models

The **Dynamic portioning / Adaptive fill / Volume fill** switch at the top of the
window chooses which algorithm runs.

**Volume fill** is dynamic portioning steered by volume alone: in the same `step_g`
passes, and ignoring the weight floor, it tops up while the next whole pass fits, and
steps bases and toppings down (never proteins, never below a set share of their
portion) when a bowl is over as written. "Fits" is a set share of the food room.
The brand report, the full sweep CSV and `menu_report.qmd` all carry it. They are different shapes of thing, not two tunings of one.

For a plain-language explanation aimed at non-experts, with a worked example, see
[docs/HOW_FILLING_WORKS.md](docs/HOW_FILLING_WORKS.md).

**Dynamic portioning** (the legacy ramp) is what `dynamic_portion_algorithm.cc` does today: add a flat
`step_increment_g` to every ingredient, pass after pass, until a *mass* floor is
cleared. It has no volume term at all.

**Adaptive** is what the Adaptive Dispense user stories propose: solve directly for a
*volume* target, holding each ingredient inside a tolerance band. The whole adjustment
is a single scalar — every ingredient moves the same fraction of its own band:

```
g_i(a) = clamp( nominal_i × (1 + a·tolerance_i),  min_i, max_i )
```

Total volume is monotone in `a`, so it is found by bisection: no optimiser, no local
minima. The default tolerances are **base ±20%, protein ±5%, topping ±10%**, which is
the document's tolerance table and its "4:1:2 ratio steps for base/protein/topping"
stated twice — 20:5:10 reduces to 4:1:2, so a base moves four times as far as a
protein by construction.

The target (85%) and the leave-alone band (75–85%) are shares of the **food room**:
the functional fill less what the sauce cups and any tall chunks take. Two cups
therefore lower the target, rather than eating into the bowl's headroom.

Three outcomes, matching the Day in the Life scenarios:

| status | meaning |
| --- | --- |
| **no adjustment** | nominal already sits inside the acceptable band, so nothing is touched |
| **adjusted** | solved onto the target within tolerance |
| **underfilled** | cannot reach the target even at full positive tolerance; dispensed short and flagged |
| **still over at max reduction** | will not fit even at full negative tolerance; the nominal spec itself has to change |

**Sauce cups** occupy real space in the bowl — up to two at 70 ml each (measured) — so the tiles
report both *food fill* and *with sauce cups*.

**The bowl, lid and cups were measured on 2026-10-12** and are the defaults under
*Settings → Bowl & lid*:

| | measured | works out to |
| --- | --- | --- |
| bowl | 46 mm deep inside (51 mm outside), 161 mm across the floor, 181 mm at the rim | 1058 ml to the rim |
| lid | inside rises ~5 mm over a ~160 mm centre | ~100 ml above the rim |
| sauce cup | 30 mm tall lidded, 65 mm across the lid, 45 mm at the base | 72 ml (camera read ~70) |

The 32 oz *functional fill* is a fill line, not the rim: 946 ml reaches 41.6 mm,
4.4 mm below it. With *Judge fit by height* on, food may rise to that line unless a
chunk standing proud or a resting cup would then touch the lid; pressed-in cups
(tops at the lid) take away what they displace below the surface. The overfill odds
are judged against both the food room and the real rim.

**COGS is reported, never solved for.** The document is explicit: "understand the
impact on COGS, not optimize up front on COGS." Prices come from
`data/ingredient_costs.csv`, and **every number in that file is a placeholder** —
order-of-magnitude guesses from the ingredient name, chosen so the relative ordering
is sensible. Good enough to see whether a change moves COGS the right way; not good
enough to quote. Unlisted ingredients fall back to a per-kind default and are flagged
in the UI with `~cost`.

## Units

The **fl oz / ml** toggle sits beside the theme button in the header — both are
display preferences, and both are remembered between runs.

The model works entirely in **US fluid ounces**: the measured data is recorded that
way and every fitted `K` is expressed in it. Converting inside the model would mean
re-deriving the curves, so conversion happens only at the point of display, and
anything typed into the UI is converted straight back before it reaches the model.
Switching units therefore cannot change a result — only how it reads.

Everything moves together: the verdict, the stat tiles, both charts' axes and
tooltips, the bowl diagram, the breakdown table, the fitted-curve line
(`ml = 9.196·g^0.938` is the same curve as `oz = 0.311·g^0.938`), the bowl-capacity
box and the per-ingredient rate column.

**CSV import accepts either.** A sheet with an `ml` column (or `millilitres`,
`volume_ml`, `cc`) is converted on the way in, so a metric measurement lands on the
same curve an imperial one would. `1 fl oz = 29.5735295625 ml`, exact by definition.

## Command line

```
--assets <dir>      where menus/, data/ and photos/ live
--select "Brand|Recipe"
--size WxH
--scale n           render at this device pixel ratio
--units oz|ml
--mode legacy|adaptive
--dark
--screenshot <file> render to PNG and exit
```

`--screenshot` works under `QT_QPA_PLATFORM=offscreen`, which is how the layout is
checked without a display server. Pair it with `--scale 2` for a sharp PNG — an
offscreen surface is 1x otherwise, and small chart labels suffer for it.

## Exporting Volume Fill to lab37

lab37 runs Volume Fill in `lab37/orders/volume_fill_algorithm.cc`, reading its curves
and settings from the generated `lab37/orders/volume_fill_curves.cc`. To update that
file after new measurements, mappings or settings:

```
./build/bowl-fill-simulator --export-volume-fill ~/Projects/lab37/lab37/orders/volume_fill_curves.cc
```

It writes one row per brand and production ingredient spelling (as customizations name
it), with the Robot-method curve the window would choose, and the fit share, minimum
share and bowl, lid and cup dimensions last set in the window. Compression is on with a
load transfer of 1 and the capacity is 32 oz, the window's defaults. A brand turns it on
by setting `algorithm` (`Shadow` or `VolumeFill`) and `volume_fill_brand` (the brand's
name here, e.g. `ORO`) on its `dynamic_portion_increases` entry.

The fit target and step-down floor exported here are the defaults for every brand. A
brand can override either without a release by setting `volume_fill_fit_share` or
`volume_fill_min_share` on the same entry, as a fraction (`0.8` is 80% of the food room).

## Known limits

- Proteins and toppings have **no measurements**. They use a flat oz/100 g, editable
  per ingredient, and are treated as incompressible.
- White Rice and Brown Rice and Lentils have no curve of their own and borrow
  Mexican Rice's, flagged in the UI as a proxy.
- The bundled menu dumps are a 2026-09-17 snapshot and already trail production:
  romaine's cap is now 80 g and its minimum 15 g, and Cowgirl's kale cap is 80 g.
  Re-pull before drawing conclusions from the exact numbers.
