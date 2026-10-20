"""Fit the mass-to-volume curves as one hierarchical Bayesian model and write
data/fitted_curves.json for the app.

Every ingredient keeps its own power law, ml = A * (g / 100)^p, but the curves are fitted
together: ingredients in a pool group (data/ingredient_groups.csv; grain, green, protein
or topping by default) share a typical size, all foods share a typical shape, and every
measurement file is a batch whose volume can sit a little above or below the
ingredient's typical batch. An ingredient with plenty of data follows its own points; one
with few is pulled toward its group, in proportion to how little its points say. The
notebook curve_model_selection.ipynb (section 10) walks through the model.

With no arguments it asks the app for the points it loads (so the camera zero, scale_g,
backfill and pool groups are exactly the app's) and writes data/fitted_curves.json:

    uv run tools/fit_curves.py

or, with the fit input written separately:

    ./build/bowl-fill-simulator --export-fit-input fit_input.csv
    uv run tools/fit_curves.py fit_input.csv --out data/fitted_curves.json

Sampling takes about a minute. Review the diff and commit the JSON with the
measurements it was fitted on.
"""

import argparse
import io
import json
import math
import subprocess
import sys
import warnings
from datetime import datetime
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parent.parent
ML_PER_FL_OZ = 29.5735295625
SCHEMA = 1

# Weak priors on physical scales; see the notebook for the reasoning.
PRIORS = {
    "mu_logA": {"dist": "Normal", "mu": math.log(150), "sigma": 0.7, "meaning": "typical ml at 100 g, per group"},
    "tau_A": {"dist": "HalfNormal", "sigma": 0.5, "meaning": "spread of sizes within a group (log)"},
    "mu_p": {"dist": "Normal", "mu": 1.0, "sigma": 0.2, "meaning": "typical exponent"},
    "tau_p": {"dist": "HalfNormal", "sigma": 0.15, "meaning": "spread of exponents between foods"},
    "tau_batch": {"dist": "HalfNormal", "sigma": 0.15, "meaning": "batch-to-batch shift (log)"},
    "sigma": {"dist": "HalfNormal", "sigma": 40.0, "meaning": "bowl-to-bowl scatter, ml"},
}


def read_fit_input(source):
    """The app's fit input: a '# fingerprint: ...' line, then ingredient,group,batch,g,ml."""
    text = Path(source).read_text() if source != "-" else sys.stdin.read()
    first, _, body = text.partition("\n")
    if not first.startswith("# fingerprint:"):
        sys.exit("fit input has no '# fingerprint:' line; write it with --export-fit-input")
    fingerprint = first.split(":", 1)[1].strip()
    data = pd.read_csv(io.StringIO(body))
    missing = {"ingredient", "group", "batch", "g", "ml"} - set(data.columns)
    if missing:
        sys.exit(f"fit input is missing columns: {', '.join(sorted(missing))}")
    return fingerprint, data


def export_from_app(binary):
    out = subprocess.run([str(binary), "--export-fit-input", "-"], capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"{binary} --export-fit-input failed:\n{out.stderr}")
    tmp = io.StringIO(out.stdout)
    first = tmp.readline()
    return first.split(":", 1)[1].strip(), pd.read_csv(tmp)


def fit(data, draws, tune, chains, seed, target_accept):
    import pymc as pm

    ing_idx, ingredients = pd.factorize(data.ingredient)
    group_of = data.groupby("ingredient", sort=False).group.first().reindex(ingredients)
    grp_idx, groups = pd.factorize(group_of)
    batch_idx, batches = pd.factorize(data.ingredient + " | " + data.batch)
    x = np.log(data.g.to_numpy() / 100)
    y = data.ml.to_numpy()

    coords = {"group": list(groups), "ingredient": list(ingredients), "batch": list(batches)}
    with pm.Model(coords=coords):
        # Non-centred: mean + spread * standard normal, which samples far better here.
        mu_logA = pm.Normal("mu_logA", PRIORS["mu_logA"]["mu"], PRIORS["mu_logA"]["sigma"], dims="group")
        tau_A = pm.HalfNormal("tau_A", PRIORS["tau_A"]["sigma"])
        logA = pm.Deterministic("logA", mu_logA[grp_idx] + tau_A * pm.Normal("z_A", 0, 1, dims="ingredient"),
                                dims="ingredient")
        mu_p = pm.Normal("mu_p", PRIORS["mu_p"]["mu"], PRIORS["mu_p"]["sigma"])
        tau_p = pm.HalfNormal("tau_p", PRIORS["tau_p"]["sigma"])
        p = pm.Deterministic("p", mu_p + tau_p * pm.Normal("z_p", 0, 1, dims="ingredient"), dims="ingredient")
        tau_batch = pm.HalfNormal("tau_batch", PRIORS["tau_batch"]["sigma"])
        b = pm.Deterministic("b", tau_batch * pm.Normal("z_b", 0, 1, dims="batch"), dims="batch")
        sigma = pm.HalfNormal("sigma", PRIORS["sigma"]["sigma"], dims="ingredient")
        mean_ml = pm.math.exp(logA[ing_idx] + b[batch_idx] + p[ing_idx] * x)
        pm.Normal("ml", mean_ml, sigma[ing_idx], observed=y)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            idata = pm.sample(draws, tune=tune, chains=chains, target_accept=target_accept, random_seed=seed,
                              progressbar=sys.stderr.isatty())
    return idata, list(groups)


def diagnostics(idata):
    import arviz as az

    summary = az.summary(idata, var_names=["mu_logA", "tau_A", "mu_p", "tau_p", "tau_batch", "logA", "p", "sigma"])
    return {
        "divergences": int(idata.sample_stats.diverging.sum()),
        "max_rhat": float(summary.r_hat.max()),
        "min_ess_bulk": float(summary.ess_bulk.min()),
        "draws": int(idata.posterior.sizes["chain"] * idata.posterior.sizes["draw"]),
    }


def summarise(data, idata, groups, seed):
    """Per-ingredient and per-group curves, in the app's units (fl oz)."""
    rng = np.random.default_rng(seed)
    post = idata.posterior.stack(sample=("chain", "draw"))
    S = post.sizes["sample"]
    tau_b = post["tau_batch"].values

    def rel_sd(draws):
        return float(np.std(draws) / np.mean(draws))

    def curve(logA, p):
        K_ml = math.exp(float(np.mean(logA))) / 100 ** float(np.mean(p))
        return {"k_oz": K_ml / ML_PER_FL_OZ, "k_ml": K_ml, "p": float(np.mean(p))}

    ingredients, scatter_by_group = [], {}
    for name, d in data.groupby("ingredient", sort=True):
        logA = post["logA"].sel(ingredient=name).values
        p = post["p"].sel(ingredient=name).values
        sig = post["sigma"].sel(ingredient=name).values
        ref_g = float(np.median(d.g))
        typical = np.exp(logA + p * math.log(ref_g / 100))
        # One bowl from a batch nobody has measured: a fresh batch shift, plus bowl scatter.
        new_batch = typical * np.exp(rng.normal(0, tau_b)) + sig * rng.standard_normal(S)
        scatter = float(np.mean(sig) / np.mean(typical))
        group = d.group.iloc[0]
        scatter_by_group.setdefault(group, []).append(scatter)
        ingredients.append({
            "name": name, "group": group, **curve(logA, p),
            "p_94": [float(v) for v in np.percentile(p, [3, 97])],
            "n": int(len(d)), "batches": int(d.batch.nunique()),
            "lo_g": float(d.g.min()), "hi_g": float(d.g.max()), "ref_g": ref_g,
            "scatter_rel_sd": scatter, "new_batch_rel_sd": rel_sd(new_batch),
        })

    out_groups = []
    for group in groups:
        members = data[data.group == group]
        mu = post["mu_logA"].sel(group=group).values
        logA = rng.normal(mu, post["tau_A"].values)
        p = rng.normal(post["mu_p"].values, post["tau_p"].values)
        ref_g = float(np.median(members.g))
        typical = np.exp(logA + p * math.log(ref_g / 100))
        rel_scatter = float(np.mean(scatter_by_group[group]))
        new_ingredient = typical * np.exp(rng.normal(0, tau_b)) * (1 + rel_scatter * rng.standard_normal(S))
        out_groups.append({
            "name": group, **curve(mu, post["mu_p"].values),
            "members": sorted(members.ingredient.unique().tolist()),
            "n": int(members.ingredient.nunique()), "batches": int(members.batch.nunique()),
            "lo_g": float(members.g.min()), "hi_g": float(members.g.max()), "ref_g": ref_g,
            "scatter_rel_sd": rel_scatter, "new_batch_rel_sd": rel_sd(new_ingredient),
        })
    return ingredients, out_groups, float(np.exp(np.mean(tau_b)) - 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", nargs="?", help="fit input from --export-fit-input (default: ask the app)")
    ap.add_argument("--out", default=str(ROOT / "data" / "fitted_curves.json"), help="where to write the JSON")
    ap.add_argument("--binary", default=str(ROOT / "build" / "bowl-fill-simulator"),
                    help="the app, when asking it for the fit input")
    ap.add_argument("--draws", type=int, default=1000)
    ap.add_argument("--tune", type=int, default=1500)
    ap.add_argument("--chains", type=int, default=4)
    ap.add_argument("--seed", type=int, default=37)
    ap.add_argument("--target-accept", type=float, default=0.99,
                    help="NUTS step-size target; higher is slower but avoids divergences")
    args = ap.parse_args()

    fingerprint, data = read_fit_input(args.input) if args.input else export_from_app(args.binary)
    print(f"fitting {len(data)} points: {data.ingredient.nunique()} ingredients, "
          f"{data.group.nunique()} groups, {data.groupby(['ingredient', 'batch']).ngroups} batches", file=sys.stderr)

    idata, groups = fit(data, args.draws, args.tune, args.chains, args.seed, args.target_accept)
    diag = diagnostics(idata)
    ingredients, out_groups, batch_rel_sd = summarise(data, idata, groups, args.seed)

    result = {
        "schema": SCHEMA,
        "fitted_at": datetime.now().astimezone().isoformat(timespec="seconds"),
        "fit_input_fingerprint": fingerprint,
        "model": "ml = A_i * (g/100)^p_i * exp(b_batch); log A_i ~ N(mu_A[group], tau_A); "
                 "p_i ~ N(mu_p, tau_p); b ~ N(0, tau_batch); ml ~ N(curve, sigma_i)",
        "priors": PRIORS,
        "diagnostics": diag,
        "batch_rel_sd": batch_rel_sd,
        "ingredients": ingredients,
        "groups": out_groups,
    }
    Path(args.out).write_text(json.dumps(result, indent=2) + "\n")

    ok = diag["divergences"] == 0 and diag["max_rhat"] <= 1.01
    print(f"{'ok' if ok else 'CHECK'}: {diag['divergences']} divergences, max r_hat {diag['max_rhat']:.3f}, "
          f"min bulk ESS {diag['min_ess_bulk']:.0f}", file=sys.stderr)
    print(f"a new batch typically shifts volume by ±{100 * batch_rel_sd:.0f}%", file=sys.stderr)
    for c in ingredients:
        print(f"  {c['name']:28s} {c['group']:8s} n={c['n']:3d}  p={c['p']:.3f}  "
              f"new-batch SD {100 * c['new_batch_rel_sd']:.0f}%", file=sys.stderr)
    print(f"wrote {args.out}", file=sys.stderr)
    if not ok:
        print("The sampler had trouble; look before committing (more --tune, or check the data).", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
