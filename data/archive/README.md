# Archived measurements

Files set aside from `data/measurements/`. The app does not load them and the fitted
curves are not fitted on them. Move a file back and refit (`uv run tools/fit_curves.py`)
to use it again.

## bowl_data_20261007.csv: lemon couscous, 2026-10-07 (set aside 2026-10-20)

Six single-portion couscous bowls from the first measurement day. They read 20-25%
less volume per gram than the 41 couscous bowls of 2026-10-09. Romaine also read low
on 2026-10-07, and that day's mixed bowls agreed with its single bowls, which points to
a measurement problem on that day (camera calibration or setup) rather than a real
batch difference. Kept in, this one session drove most of the fitted batch-to-batch
spread (±15% against ±10% without it), and so widened every ingredient's predicted range.

This is provisional, not proof of error: couscous density does vary with hydration and
steaming. **Bring it back** if new couscous batches show shifts of this size (in which
case 2026-10-09 may be the odd one out), or if records show the camera was fine that day.
The other 2026-10-07 files stay in, although by the same reasoning they are suspect too.
Four ingredients (Chicken Al Pastor, Suadero Beef, Mexican Slaw, Pineapple-Cucumber
Salad) were measured only that day.

## bowl_data_20261019_2.csv: BB-0101 lab lunch, 2026-10-19

The lab lunch bowls were lidded before they were scanned (lid on, weigh, lid off, scan),
so lid compression carries into the reading. The simulator predicts volume before the lid
goes on, so these bowls stay out. `curve_model_selection.ipynb` section 9 analyses them.

## mass_to_volume_2026-09_original.csv

The original September curve set (robot- and hand-filled bowls), kept for reference.
