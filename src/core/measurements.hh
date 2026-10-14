#pragma once

#include "bowl_data.hh"
#include "curves.hh"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bowlfill
{

///
/// The measurement store: every .csv in data/measurements/, loaded at start-up.
///
/// It is the only place measured data lives. Uploading a file copies it in; editing
/// or deleting a file (or rows in it) and reloading is how data is corrected or
/// dropped. Nothing is held only in memory, so a restart never loses anything.
///
/// Two layouts are accepted, told apart by the header:
///   - curve rows: an `ingredient` column with `g` and `ml` (or `oz`), one row per
///     measurement (CurveSet::format_help)
///   - bowl sheets: a column per ingredient holding grams, then `ml`, one row per
///     scanned bowl (bowl_data_format_help). Single-ingredient rows extend the
///     curves; mixed rows feed the squash fit.
///
struct MeasurementFile
{
    std::string name; ///< file name within the folder
    bool bowl_sheet = false;
    int rows = 0;        ///< usable rows read
    std::string problem; ///< why it could not be read, or rows it skipped
    bool failed = false; ///< nothing usable came from this file
};

///
/// How the camera distorts a reading, learned from a rigid calibration reference: a
/// single-ingredient ladder of something that cannot compress (the "test beans" run).
/// Its true volume is proportional to its mass, so its readings fitted as
/// read = K * g^p show the camera's distortion directly, and inverting that fit maps
/// any reading back to what it should have been. The correction is anchored at the
/// reference's deepest reading, where it is 1; readings above the reference's range
/// are left alone rather than extrapolated.
///
struct Backfill
{
    bool valid = false;
    std::string reference; ///< the ingredient(s) used, e.g. "test beans"
    int points = 0;
    double K = 0.0, p = 1.0;
    double top_oz = 0.0; ///< the reference fit at its heaviest reading

    double corrected_oz(double reading_oz) const;
    /// Multiplier applied to a reading of `ml` millilitres.
    double factor_at_ml(double ml) const;
};

/// A reference needs at least this many readings spanning a range of masses.
constexpr int kMinReferencePoints = 5;

/// Readings below a food's solids, mass / this density (a little denser than water),
/// are physically impossible: a pile cannot occupy less than what it is made of.
constexpr double kSolidDensityGPerMl = 1.05;

/// Calibration references are named "test ...": they are measured, but are not food.
bool is_reference_name(const std::string &ingredient);

struct MeasurementOptions
{
    double empty_bowl_ml = 0.0;   ///< the camera's zero error, applied to scans
    bool backfill = false;        ///< correct scans by the calibration reference
    bool drop_impossible = false; ///< leave readings below solid volume out of fits
};

struct MeasurementSet
{
    std::string dir;
    std::vector<MeasurementFile> files;
    BowlSheet bowls; ///< every bowl-sheet row, merged across files
    bool folder_missing = false;

    Backfill backfill; ///< valid when backfill was asked for and possible
    bool backfill_requested = false;
    /// Readings left out as impossible, by ingredient ("mixed bowl" for mixed rows).
    std::map<std::string, int> dropped;

    int failed_files() const;
    int dropped_total() const;
};

constexpr const char *kMeasurementsDir = "/data/measurements";

/// Clears `curves`, then loads every .csv in `dir` into it (sorted by name, hidden
/// and lock files skipped) and refits. The options apply to bowl sheets (camera
/// scans) only; curve-row files are taken as given, since they may come from other
/// methods. Nothing on disk is changed: turning an option off restores every reading.
MeasurementSet load_measurements(const std::string &dir, CurveSet &curves, const MeasurementOptions &options = {});

/// Fits the camera's distortion from reference readings (grams, scanned ounces).
Backfill fit_backfill(const std::vector<std::pair<double, double>> &reference, const std::string &name);

/// The volume-vision rig reads about -5 ml for an empty bowl (-4 to -8 when in
/// calibration); the app's default for the setting above.
constexpr double kDefaultEmptyBowlMl = -5.0;

/// True when the file's header names an `ingredient` column: curve rows rather than
/// a bowl sheet.
bool is_curve_csv(const std::string &path);

///
/// Copies `source` into `dir` so it is loaded from then on. A file already in `dir`
/// is left where it is. If the name is taken by a different file, a numbered suffix
/// is added; if it is taken by an identical file, nothing is copied. Returns the
/// stored path, or an empty string with `error` set.
///
std::string store_measurement_file(const std::string &source, const std::string &dir, std::string *error = nullptr);

} // namespace bowlfill
