#pragma once

#include "curves.hh"

#include <string>
#include <vector>

namespace bowlfill
{

///
/// Fitted curves come from tools/fit_curves.py, a hierarchical Bayesian fit run outside
/// the app (it needs Python and PyMC). The app hands it the exact points it loads, and
/// reads back data/fitted_curves.json:
///
///     ./build/bowl-fill-simulator --export-fit-input fit_input.csv
///     uv run tools/fit_curves.py fit_input.csv --out data/fitted_curves.json
///
/// (tools/fit_curves.py does both when run with no arguments.) The input carries a
/// fingerprint, which the fit copies into its output, so the app can tell when the
/// measurements or the pool groups have changed since the curves were fitted.
///

/// The single-ingredient points the curves rest on, one row each, as
/// ingredient,group,batch,g,ml (ml as read, after the camera's zero), sorted so the
/// same data always gives the same text. Calibration references are left out.
std::string fit_input_csv(const CurveSet &curves);

/// A short, stable hash of the fit input (64-bit FNV-1a, in hex).
std::string fit_fingerprint(const std::string &fit_input);

/// The fit input with a "# fingerprint: ..." first line, ready for tools/fit_curves.py.
bool write_fit_input(const std::string &path, const CurveSet &curves, std::string *error = nullptr);

/// Reads data/fitted_curves.json. A missing file is not an error: `out` stays unloaded.
bool load_fitted_curves(const std::string &path, FittedCurves &out, std::string *error = nullptr);

enum class FitState
{
    Missing, ///< no fitted curves: every curve is least squares on its own points
    Current, ///< fitted on exactly the measurements and groups loaded now
    Stale    ///< measurements or groups changed since the fit
};

struct FitStatus
{
    FitState state = FitState::Missing;
    std::vector<std::string> unfitted; ///< measured ingredients the fitted file does not cover
};

FitStatus fit_status(const CurveSet &curves);

/// The command that refits, for banners and messages.
std::string refit_command();

/// One plain sentence for a banner or the terminal; empty when the fit is current.
std::string fit_status_message(const FitStatus &status, const FittedCurves &fitted);

} // namespace bowlfill
