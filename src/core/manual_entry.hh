#pragma once

#include "menu_model.hh"

#include <string>
#include <vector>

namespace bowlfill
{

///
/// The "Manual Entry" brand: a menu made from what is loaded on the robot rather than
/// from a menu export. Each station holds one ingredient and says what it serves as,
/// so any station can be put in a bowl as a base, protein, topping or sauce. Sauces
/// the robot does not dispense from a station are listed on their own.
///
/// Ingredients any brand configures bring that brand's ramp settings and portions;
/// a name no menu carries is written in, and borrows typical settings for its kind
/// (WeightSource::Class), so it shows as an estimate.
///
inline const std::string kManualEntryBrand = "Manual Entry";

struct ManualStation
{
    std::string name; ///< empty for an unused station
    std::string kind; ///< one of kMappingKinds: "base", "protein", "topping", "sauce"

    bool operator==(const ManualStation &) const = default;
};

struct ManualEntry
{
    static constexpr int kStations = 19;

    std::vector<ManualStation> stations = std::vector<ManualStation>(kStations);
    std::vector<std::string> sauces; ///< served in cups, not from a station
    double floor_g = 0.0;            ///< one weight floor for every bowl; 0 for none

    bool empty() const;
};

/// Every ingredient and sauce any loaded brand serves, sorted, without duplicates.
std::vector<std::string> ingredient_catalog(const std::vector<Menu> &menus);
/// Sauces alone, as the brands list them.
std::vector<std::string> sauce_catalog(const std::vector<Menu> &menus);

/// What a name serves as: the kind most brands give it, or a guess from the name for
/// one no brand carries.
std::string catalog_kind(const std::vector<Menu> &menus, const std::string &name);

/// Builds the brand from the stations, in station order. `menus` are the loaded
/// brands; any Manual Entry among them is ignored.
Menu manual_entry_menu(const std::vector<Menu> &menus, const ManualEntry &entry, const PieceHeights *pieces = nullptr);

} // namespace bowlfill
