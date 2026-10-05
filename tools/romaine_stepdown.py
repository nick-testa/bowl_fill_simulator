"""Turn romaine-fill's per-bowl output into a romaine portion that steps down with
protein and topping count, per brand.

Each portion is the romaine weight that the given share of combinations at that count
can hold (default: 90%, i.e. the 10th percentile of solved romaine), rounded down to a
whole step. A combination with no room for romaine counts as 0 g, so it pulls the
portion down rather than dropping out.

    uv run python tools/romaine_stepdown.py romaine_fill_detail.csv --out stepdown.csv
"""

import argparse

import pandas as pd

ROMAINE_MEASURED_LO_G = 67   # robot romaine curve was measured from 67 g up
ROMAINE_MIN_G = 15           # current per_portion_minimum_weight_g in production


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("detail", help="CSV written by romaine-fill --detail")
    ap.add_argument("--fit-share", type=float, default=0.90,
                    help="share of combinations the portion must fit (default 0.90)")
    ap.add_argument("--cups", type=int, default=2, help="sauce cups (default 2)")
    ap.add_argument("--compress", type=int, default=0, choices=[0, 1],
                    help="1 to use the compressed (loaded) model (default 0)")
    ap.add_argument("--split", default="half_mass", choices=["half_mass", "half_volume"],
                    help="how the second base is halved on double bowls")
    ap.add_argument("--step", type=float, default=5, help="round down to this many g")
    ap.add_argument("--out", help="write the step-down table here as CSV")
    args = ap.parse_args()

    d = pd.read_csv(args.detail, dtype={"second_base": str, "split_mode": str})
    d = d[(d.sauce_cups == args.cups) & (d.compress == args.compress)]
    d = d[(d.base_config == "single") | (d.split_mode == args.split)].copy()
    d.loc[d.status == "no_room", "romaine_g"] = 0.0

    # Kale takes far more room per gram than the grains, so it gets its own column.
    d["config"] = "single"
    dbl = d.base_config == "double"
    d.loc[dbl, "config"] = d.loc[dbl, "second_base"].map(
        lambda b: "double_kale" if b == "Massaged Kale" else "double_grain")

    q = 1 - args.fit_share
    g = (d.groupby(["brand", "config", "n_proteins", "n_toppings"])
          .romaine_g.agg(combinations="size", raw_g=lambda s: s.quantile(q))
          .reset_index())
    g["portion_g"] = (g.raw_g // args.step) * args.step
    # What share of combinations actually fit at the rounded portion.
    fits = d.merge(g[["brand", "config", "n_proteins", "n_toppings", "portion_g"]])
    fits["fits"] = fits.romaine_g >= fits.portion_g
    g = g.merge(fits.groupby(["brand", "config", "n_proteins", "n_toppings"])
                    .fits.mean().rename("share_fit").reset_index())
    g["flag"] = ""
    g.loc[g.portion_g < ROMAINE_MEASURED_LO_G, "flag"] = "extrapolated"
    g.loc[g.portion_g < ROMAINE_MIN_G, "flag"] = "below_min"

    if args.out:
        g.to_csv(args.out, index=False)

    print(f"Romaine portion (g) that {args.fit_share:.0%} of combinations hold at 85% fill; "
          f"{args.cups} sauce cups, compress={args.compress}, doubles {args.split}.\n"
          f"* below the measured romaine range ({ROMAINE_MEASURED_LO_G} g), "
          f"! below the {ROMAINE_MIN_G} g minimum\n")
    mark = {"": "", "extrapolated": "*", "below_min": "!"}
    g["cell"] = g.portion_g.map("{:.0f}".format) + g.flag.map(mark)
    for brand, b in g.groupby("brand"):
        t = b.pivot_table(index=["n_proteins", "n_toppings"], columns="config",
                          values="cell", aggfunc="first")
        t = t[[c for c in ["single", "double_grain", "double_kale"] if c in t]]
        print(f"## {brand}\n")
        print(t.to_markdown())
        print()


if __name__ == "__main__":
    main()
