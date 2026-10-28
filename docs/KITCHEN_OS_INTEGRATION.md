# Putting Volume Fill on the Kitchen OS order path

*Feasibility report, 2026-10-13. No code has been written.*

## Short answer

**Yes, and it fits more easily than you might expect.** There is one big piece of work: moving the simulator's core onto plain C++ without Qt. There is one question for the org to settle: today's legacy portioning algorithm runs in the **Bridge Service**, which is marked frozen and is being decommissioned.

These things make it easier:

- **Same language, same kind of function.** Today's algorithm is a pure C++ function with no I/O: `maybe_apply_dynamic_portion_alogithm(Product, DynamicPortionIncreaseSettings)` (`lab37/orders/dynamic_portion_algorithm.hh`). Its one call site loops over the products (`lab37/orders/menu_mappings_util.cc:937-945`). The simulator's `volume_fill()` is also pure C++ with no I/O, and it does the same job.
- **Same input data.** The `menus/*.json` files the simulator loads are bridge `menu_mappings` dumps. Its weight floors and step sizes come from the same `dynamic_portion_increases` block that the production algorithm reads.
- **Deterministic and fast.** The core has no random numbers; overfill odds use closed-form maths. One bowl takes microseconds. This matters because the bridge re-converts every unacknowledged order on every 1-second robot poll, so the answer has to come out the same each time.

## How it works today

```
Otter/Jotform webhook ─▶ bridge: event_queue table (Postgres)
                                   │  robot polls every ~1 s
                                   ▼
            bridge: to_otter_order_creation_notification (event_queue.cc:1007)
              └─ otter::to_order (convert_order.cc:394)
                   └─ apply_rules_to_order (menu_mappings_util.cc:916)
                        ├─ product_rules (e.g. 50:50 base split)
                        └─ dynamic_portion_increases   ◀── what we would swap
                                   ▼
            robot: QueueOrderCommand ─▶ order manager ─▶ dispensers
                     (estimate_product_volume_oz, optional hold if > 44 oz × max_fill%)
```

What the current algorithm does:

- It only works in **grams**. If a bowl's summed weight is below the matching filter's `minimum_product_weight_g`, it adds `step_increment_g` to each listed ingredient. It keeps doing this until the bowl reaches the floor or every ingredient hits its max or a combined limit. Then it rounds every ingredient to 5 g.
- It **never reduces** a portion.
- It has no idea of volume.

## What the replacement does

`volume_fill()` (`src/core/simulator.cc:377`) keeps the same per-ingredient step and max rules and adds volume:

- **If the bowl fits as written,** it ramps up toward the weight floor, one whole step at a time, but only while the next step still fits in the food room (`fit_share × room`).
- **If the bowl is over,** it steps **bases and toppings down** (never proteins), but not below `min_share` (70%) of the recipe portion.
- **It returns** the predicted volume, a status (`FitsAtFloor`, `ShortRoomBound`, `SteppedDown`, `StillOver`, …) and overfill odds (`p_over_room`, `p_over_rim`).

## What has to change

### 1. Make the simulator core build without Qt (the largest task)

- `bowlfill_core` links `Qt6::Core` and uses `QString`, `QJson*`, `QFile`, `QHash` and `QRegularExpression` throughout. lab37's order code uses its own types (rbuf, fmt) and no Qt. Pulling Qt into the bridge build is not realistic.
- Split the core into:
  - **a pure physics library** with only the STL: `BowlItem`, `SimSettings`, `BowlGeometry`, `bowl_frame`, `food_capacity_oz`, `fill_uncertainty`, `volume_fill`, and curve evaluation (`oz = K·g^p`);
  - **a Qt loader layer** that stays in this repo: menu dumps, CSVs, the measurement store and curve fitting.
- The desktop app keeps working on top of both. The pure library goes into lab37, for example as `lab37/orders/volume_fill/`. It could be vendored or shared, but there must be one source of truth so the tool and production don't drift apart.
- `tests/verify.cc` needs to be split the same way. The physics and Volume Fill tests (around 40% of it) should travel with the library and run under lab37's gtest and ASAN/UBSAN CI.
- Mounding (300 rings with nested bisection) is the only slow path. Keep it off in production at first, or measure it.

### 2. Decide where the inputs live in Kitchen OS

The simulator needs data that the bridge doesn't have when it converts an order:

| Input | Where it lives now (simulator) | Where it could live (Kitchen OS) |
|---|---|---|
| Weight→volume curve per ingredient (`K`, `p`, scatter) | fitted from `data/measurements/*.csv` | new fields in `MenuMappingConfig` or `IngredientCatalogConfig` (rbuf) |
| Ingredient kind (base / protein / topping / sauce) | guessed from the name, fixed in `ingredient_aliases.csv` | explicit field; `IngredientCatalogConfig.category_name` may already be enough |
| Piece height, mound slope, roll-off angle | `ingredient_pieces/mounds/rolloff.csv` | same new per-ingredient fields |
| Bowl / lid / sauce-cup geometry, dispense SD and bias | QSettings in the desktop app | a per-brand or per-store `VolumeFillSettings` block |
| Weight floor, step, max, combined limits | `dynamic_portion_increases` (already shared) | no change |

The robot's per-ingredient `IngredientDensityConfig` is **not** a replacement for the curves. It is a single linear density, it is local to each robot, and the bridge can't see it.

Recommended: add a `volume_fill` block to `MenuMappingConfig` (`lab37/orders/menu_mappings.rbuf`). The curves and settings then travel with the menu mapping, keep per-store overrides, are versioned in `brand_menu_data`, and can be exported from this tool.

### 3. Match ingredients by name, correctly

- Production dynamic-portion rules match on the `Customization.name` string. The simulator matches on a **cleaned** name (it strips `retherm`, `v11`, brackets and so on) and then applies `ingredient_aliases.csv`.
- The ingredient renames from the brand refresh make this risky. A curve that silently fails to match makes a bowl fall back to a flat oz/100 g rate, which gives a wrong prediction.
- Fix: export curves keyed by the **exact production `ingredient_name`**.
- Add a validation report, like the existing `report_dynamic_mapping_missing`, that flags any ingredient in a dynamic-portion filter that has no curve. Stores with incomplete data (Casa Condesa and M+R2 for now) would stay on the legacy algorithm.

### 4. Swap the call site

In `apply_rules_to_order`, choose a path per store or brand based on a mode setting:

- `off`: today's behaviour.
- `shadow`: run both, dispense the legacy result, and log what Volume Fill would have done.
- `on`: dispense the Volume Fill result.

Details to handle:

- **Map `Product` to `BowlItem`s.** Quantity > 1 (for example double protein) and merged duplicate customizations need a clear rule. Sauces go to cups, and `excluded_ingredients` are skipped.
- **Keep the legacy output format.** Round to 5 g, as dispensers and reports expect. Check that this rounding can't push a bowl back over.
- **Guard bad config.** Reject `step_increment_g <= 0` in Volume Fill. The legacy loop already hangs on that input, and only the UI checks for it.
- **Fail safe.** Any exception or missing data should fall back to the legacy algorithm, not fail order conversion.

### 5. Customer notes run after the adjustment

`order_note.cc:463` can scale weights *after* `apply_rules_to_order`, for example for "extra rice". A volume-checked bowl can then become overfull. You can move the volume pass after note handling, or accept the risk and let the robot-side volume hold (`hold_order_exceeding_volume_limits`) catch it.

### 6. Visibility and validation (needed before turning it on)

- The pre-adjustment weights are lost today. Add these fields to the order or product (rbuf): `nominal_g` per customization, `predicted_volume_oz`, `p_over_room`, the Volume Fill status, and the model version.
- Carry them into `order_report` and the triage parquet. Then you can compare predicted fill with what actually happened (fill-level camera, overfill and anomaly reports) and tune `fit_share`, `min_share` and the curves.
- Use `shadow` mode for at least a few weeks per brand first.

### 7. Settle the policy questions with ops and finance

You described the goal as adjusting volume up or down based on bowl selection criteria. The code can do that, but someone has to choose the criteria:

- **Which bowls to adjust:** only predicted over or short, or anything outside a target band? Should the trigger be a hard threshold on predicted oz, or on `p_over_room`?
- **Lowest acceptable portion:** is 70% of a base or topping acceptable to customers and for pricing? Should proteins always be protected?
- **Short bowls:** should a bowl the room can't fill to the weight floor (`ShortRoomBound`) ship light, or should it be flagged?
- **Cost:** COGS impact. The simulator reports each bowl's food cost (the brand sweep carries `volume_cost`); Volume Fill doesn't act on it.

## Where should it run? Three options

| Option | Pros | Cons |
|---|---|---|
| **A. Bridge Service** (replace the call in `apply_rules_to_order`) | Same place as today, smallest change, one decision per order that every downstream consumer sees, deterministic | Bridge is "frozen / being decommissioned", so it needs sign-off; the work will have to be moved again later |
| **B. Robot order manager** (next to `estimate_product_volume_oz`) | Has local density and calibration, already has a volume-hold hook, and sees note edits | Robot release cadence; each robot could decide differently; the adjustment happens after the bridge's reports are written |
| **C. New Kitchen OS stack** (kos_service, Go) | Where the platform is going | Not on the order path today. It needs a Go port or a C++ sidecar, plus a new step in the order path; by far the most work |

**Recommendation: A, inside a library you can lift out.** Make the pure C++ library small and self-contained so it moves to wherever orders get converted next. Raise the bridge freeze with whoever owns the decommission before starting.

## An easier interim step: tune the existing config offline

Before any production code changes, the simulator's `--sweep` and `--audit` modes can tune today's `dynamic_portion_increases` settings: floors, per-ingredient maxes and combined limits. The result would be that the legacy weight ramp stops before it overfills, for example by lowering the rice floor or capping romaine. `tools/romaine_stepdown.py` already does this for romaine.

- **What it can do:** stop the ramp-up from overfilling bowls.
- **What it can't do:** shrink a bowl that is too big as ordered.
- **Changes needed:** none on the production side; it ships through KOS Web.
- **Benefit:** it gives an early read on how good the model is.

## Suggested order of work

1. **Offline config tuning** with the existing sweep: quick wins, no production code.
2. **Split the core** into a Qt-free physics library and a Qt loader, with tests split to match.
3. **Add data to Kitchen OS:**
   - add the rbuf schema for curves and settings;
   - add an export from this tool;
   - add the missing-curve validation report.
4. **Bridge integration in `shadow` mode,** with logging and report fields.
5. **Compare shadow predictions** with the fill camera and overfill data, then tune.
6. **Switch to `on`** brand by brand, starting with the brands with the most measurement data.
