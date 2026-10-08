# Measurements

Every `.csv` file in this folder is loaded each time the simulator starts, and
again when you press **Reload** (Advanced → Mass → volume data). This folder is
the only source of measured data, so what is here is what the curves are fitted to.

- **Add data:** use Upload CSV… or Upload bowl data… in the app (the file is copied
  here), or drop a file in this folder and press Reload.
- **Fix or remove data:** edit the file, or delete rows or whole files, then Reload.
- **Set data aside without deleting it:** move the file to `../archive/`.

Files whose names start with a dot (such as LibreOffice's `.~lock…#` files) and files
that are not `.csv` (like this one) are ignored. A file that cannot be read is named
in the app and skipped; the rest still load.

## How readings are used

**Camera zero.** Bowl sheets are camera scans, and the camera reads an empty bowl at
about -5 ml. Every scanned volume is corrected by the **Empty-bowl reading** setting
(Advanced, default -5 ml) before curves are fitted, so a raw -3 ml counts as 2 ml.
The files themselves keep the raw readings. Curve-row files (`ingredient, g, ml`)
are taken as given, since they may come from other methods.

**Curve fitting.** Each ingredient's curve, ml = K·g^p, is fitted by least squares
on the volume itself, so a few ml of camera error weighs the same at 20 g as at
400 g instead of dominating the small portions.

## Two layouts, told apart by the header

**Bowl sheet** (what the volume-vision workflow produces): one row per scanned bowl,
one column per ingredient holding grams (blank when absent), then `ml`. Optional
`cups`, `lid` and `notes` columns. Put ingredient columns in dispense order, left to
right: the first is the bottom layer. Single-ingredient rows become curve points;
rows with several ingredients fit the base squash. See `../bowl_data_template.csv`.

```
Romaine Base,White Jasmine Rice,Chicken Al Pastor,ml,cups,lid,notes
55,90,100,670,2,closed,
60,,,390,,,
```

**Curve rows**: an `ingredient` column, `g`, and `ml` (or `oz`), one row per
measurement. Optional `method` column: `robot` (default) or `hand`.

```
method,ingredient,g,ml
robot,Romaine Base,86,590
```

Ingredient names match regardless of case. A name has to match the menu's (after the
Ingredients dialog's mappings) for its curve to be used: "lemon couscous" is a
different ingredient from the menus' "Couscous".
