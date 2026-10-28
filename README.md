# Bowl Fill Simulator

A Qt6 desktop port of the HTML bowl-fill simulator. It models Dynamic Portioning —
how the robot tops up or trims a bowl — against the limit that actually makes bowls
fail at the lidder, which is **volume**. The legacy portioning algorithm optimises on
**mass**; Volume Fill, its replacement, on volume.

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
           ingredient_colours.csv — colours chosen in the Ingredients dialog
photos/    reference photographs of known masses in a bowl
src/core/  simulation, curve fitting, costs and menu parsing (plain C++, no Qt)
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
every bowl (used by legacy portioning; Volume fill ignores floors). Pick any name a
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
```

- `ingredient`, `g`, `oz` are required, in any order and any case.
  `mass`/`grams`/`mass_g` and `volume`/`ounces`/`volume_oz`/`fl_oz` also work.
- `ml` may be supplied instead of `oz` and is converted on import.
- `method` is optional and defaults to `robot`. Rows marked `hand` are skipped, and the
  upload says how many: curves are robot-only, as production is.
- Names match loosely, so `kale`, `Kale` and `Massaged Kale` land in one bucket.
- An ingredient not seen before gets a curve of its own once it has two points at
  different masses. One point is not a curve, and the app says so rather than
  fitting a line through it.

## The model

**Mass to volume** is `oz = K · g^p` per ingredient. A straight line fits the measured
bowls about as well but only by carrying an intercept (volume at zero grams), so the
power law is the honest form; `p < 1` is compaction, each added gram buying less volume
than the one before it. The curves come from the fitted file described under
[Fitted curves](#fitted-curves); without it, each ingredient is fitted alone by least
squares on the volume itself.

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

Curves are robot-only. Hand-filled bowls held about 5.8% more volume at equal mass,
concentrated in the greens, and production is robot-filled, so hand-filled
measurements are skipped.

## Fitted curves

The curves are fitted **together**, as one hierarchical Bayesian model
(`tools/fit_curves.py`, PyMC), and the app reads the result from
`data/fitted_curves.json`:

- Each ingredient keeps its own power law, but ingredients in a **pool group** share
  a typical size, and all foods share a typical shape. An ingredient with plenty of
  data follows its own points; one with a handful is pulled toward its group, in
  proportion to how little its points say.
- Every measurement file counts as one **batch**, and batches may sit above or below
  an ingredient's typical batch. How much they vary is learned from the ingredients
  measured on more than one day, and applies to all of them.
- An ingredient with **no measurements** takes its group's typical curve, with a
  spread that reflects how much the group's members differ. A group with nothing
  measured falls back to the flat rate.
- Each curve carries a **new-batch spread**: how far one bowl from a batch nobody has
  measured lands from the curve (batch variation, the curve's own uncertainty and bowl
  scatter together). Every range and overfill chance in the app is built from it, plus
  the dispense variation. Production never fills from a measured batch, so this is the
  honest spread.

**Pool groups** are set per ingredient in `data/ingredient_groups.csv`
(`ingredient,group`), or in the Ingredients dialog's *Pool group* column, where typing a
new name starts a group. Unlisted ingredients take an automatic group: `grain` or
`green` for a base, `protein`, `topping`, or `sauce` for a sauce (a sauce is a cup, so its
group never gets a curve). Names match whatever their case.

**Refitting.** After adding measurements or changing groups, run

```
uv run tools/fit_curves.py
```

It asks the app for exactly the points it loads (`--export-fit-input`, so the camera
zero, `scale_g`, backfill and the drop-impossible setting all apply), samples for about
a minute, and writes `data/fitted_curves.json`. Press **Reload** (or restart) to use it.
Review the diff and commit the JSON with the measurements it was fitted on. The script
stops with a warning if the sampler had trouble (divergences, r̂ above 1.01).

The JSON records a fingerprint of the points and groups it was fitted on. When they
change, the header shows **Fitted curves out of date** (and the command-line reports
say so): the old fit stays in use, and any ingredient it does not cover uses least
squares until refitted. With no JSON at all the header shows **Curves not fitted**, and
every curve is least squares with no batch term.

**Overfill risk.** Settings → Dispense sets the overfill chance bowls are flagged at
(10% by default), in the bowl panel and the brand report. It is for reporting only:
Volume Fill still decides with its fit target.

The model, its priors and what partial pooling does to each ingredient are walked
through in `curve_model_selection.ipynb` (sections 10 and 11).

## Tests

```bash
./build/bowlfill-verify     # or: ctest --test-dir build
```

389 checks, many asserted against the HTML simulator this was ported from: the fitted
curves, menu parsing including the template/standalone split, floor matching, six
end-to-end recipes, the compression model's zero-load identity and its inverse (grams
for a volume), costs, Volume Fill, and CSV ingest including metric input and malformed
input.

## The Bowl Builder

The window opens on the **Bowl Builder**, for building a bowl the way a chef thinks of
one. Pick a menu (or Manual Entry, for whatever is loaded on the robot), start from one
of its bowls or build your own, and add bases, proteins, toppings and sauces.

The bowl is shown as a cutaway of its left quarter, drawn large and true to scale (that
much of the bowl is about as wide as it is deep), with each dispensed ingredient a level
layer in dispense order and the lid, the full line and the max line across it. Drag a
layer's tab, or its top edge, to set how much of the bowl it fills; the layers above
ride along. Under the ingredient pickers, **This bowl** has the totals and a line per
ingredient, with a box for typing an exact percentage.

The page is laid out on the golden ratio: the left column and the bowl share the width
1 : φ (38% : 62%), the column's two panels share its height φ : 1, and the bowl takes
about 62% of its panel's height. The column stays between 400 and 640 px wide, and on a
very wide screen the page stops at about 1800 px and centres.

- Percentages are of the **food room**: the bowl's functional fill less what the sauce
  cups (and any tall chunks) need. Together they are how full the bowl is.
- A named recipe starts as the menu writes it: each ingredient at the recipe's grams,
  and anything it gives no weight (or the base and sauce a protein-only template leaves
  to the customer) at its menu portion. Build your own starts at the menu's portions
  scaled together to the **max fill**: the menu's `volume_fill_fit_share` where it sets
  one (BIG JERK and ORO set 0.87), otherwise Volume fill's *Fit within* in Dynamic
  Portioning. An ingredient added later comes in at its menu portion. Set the
  proportions however looks right (10% and 20% is fine), then **Scale to a full bowl**
  scales every ingredient by the same factor up (or down) to the max.
- Each percentage becomes grams through the ingredient's curve, compression included
  (`grams_for_volumes`, the inverse of the bowl's volume), so the builder's grams give
  exactly the fill it shows. Weight and volume are shown per ingredient and for the bowl,
  with the food cost from `data/ingredient_costs.csv` (sauce cups are not costed) and the
  chance a real bowl overfills. A portion above the menu's `max_dispense_weight_g` is
  shown in red.

**Dynamic Portioning →** opens the simulator on that bowl: the recipe *Your bowl, from
the Bowl Builder* pins the builder's grams as the start weights, and everything else
works as before. Going back and forth keeps Dynamic Portioning's edits until the bowl in
the builder changes.

## Dispense models

**Dynamic Portioning** is the process as a whole: what the robot does to a bowl's
portions before it dispenses. The **Legacy portioning / Volume fill** switch in the
Dynamic Portioning section chooses which algorithm runs; it opens on Volume fill, the
replacement for the legacy ramp.

**Volume fill** is the ramp steered by volume alone: in the same `step_g`
passes, and ignoring the weight floor, it tops up while the next whole pass fits, and
steps bases and toppings down (never proteins, never below a set share of their
portion) when a bowl is over as written. "Fits" is a set share of the food room.
The brand report, the full sweep CSV and `menu_report.qmd` all carry it.

For a plain-language explanation aimed at non-experts, with a worked example, see
[docs/HOW_FILLING_WORKS.md](docs/HOW_FILLING_WORKS.md).

**Legacy portioning** (the legacy ramp) is what `dynamic_portion_algorithm.cc` does today: add a flat
`step_increment_g` to every ingredient, pass after pass, until a *mass* floor is
cleared. It has no volume term at all.

The food room is the functional fill less what the sauce cups and any tall chunks
take, so two cups lower what food may fill, rather than eating into the bowl's headroom.

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

**COGS is reported, never solved for.** Prices come from
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
--mode volume|legacy   Dynamic Portioning's algorithm; Volume fill is the default
--view builder|portioning   open on Dynamic Portioning, from the builder's bowl
--dark
--screenshot <file> render to PNG and exit
```

`--screenshot` works under `QT_QPA_PLATFORM=offscreen`, which is how the layout is
checked without a display server. Pair it with `--scale 2` for a sharp PNG — an
offscreen surface is 1x otherwise, and small chart labels suffer for it.

## Exporting Volume Fill to lab37

lab37 runs Volume Fill in `lab37/orders/volume_fill_algorithm.cc`. Each ingredient's
curve comes from the Catalog (`RobotBrandConfig.ingredients[name].volume_curve`) when it
is set there, and otherwise from the defaults compiled into
`lab37/orders/volume_fill_curves.cc`; anything in the Catalog's `sauces` is a cup. To
update the defaults, and write the values the Catalog should hold, after new
measurements, mappings or settings:

```
./build/bowl-fill-simulator --export-volume-fill ~/Projects/lab37/lab37/orders/volume_fill_curves.cc \
    --catalog-manifest volume_fill_catalog.json
```

Only the brands listed in `data/active_brands.csv` are exported, so menus kept in `menus/`
for reference (deprecated brands) never put their spellings or curves into lab37. The
export names the brands it wrote and the ones it left out; delete the file to export
every brand. lab37 and the Catalog work in metric: each curve is written as `ml_at_1_g`
(the volume of 1 g, in ml) and `volume_exponent` (how volume grows with mass), and the same
values are shown under Settings → Volume per gram (*ml at 1 g*, *Exponent*, and *Copy
Catalog curves*) for updating the Catalog by hand. Only the bowl's capacity stays in ounces.

It writes one row per production ingredient spelling (as customizations and the Catalog
name it), with the curve the window would choose; a flat rate is written as
`k = rate / 100`, `p = 1`. An ingredient has one curve whichever brands serve it. Where
brands disagree (one cups an ingredient another dispenses), the dispensed curve is kept
and the disagreement is printed. The defaults also carry the fit share, minimum share and
bowl, lid and cup dimensions last set in the window; compression is on with a load
transfer of 1 and the capacity is 32 oz, the window's defaults.

A brand turns Volume Fill on by setting `algorithm` (`Shadow` or `VolumeFill`) on its
`dynamic_portion_increases` entry, and can override the fit target and step-down floor
without a release with `volume_fill_fit_share` or `volume_fill_min_share`, as fractions
(`0.8` is 80% of the food room).

## Known limits

- Proteins and toppings have **no measurements**. They use a flat oz/100 g, editable
  per ingredient, and are treated as incompressible.
- Ingredients with no measurements take their pool group's typical curve (or, with no
  fitted curves, borrow a relative's or a composite), flagged in the UI as estimates.
- The batch-to-batch spread is learned from only the few ingredients measured on more
  than one day, so it is loosely known; measuring more batches narrows every range.
- The bundled menu dumps are a 2026-09-17 snapshot and already trail production:
  romaine's cap is now 80 g and its minimum 15 g, and Cowgirl's kale cap is 80 g.
  Re-pull before drawing conclusions from the exact numbers.
