#pragma once

#include "menu_model.hh"

#include <QString>
#include <QStringList>
#include <vector>

namespace bowlfill {

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
inline const QString kManualEntryBrand = QStringLiteral("Manual Entry");

struct ManualStation {
    QString name;   ///< empty for an unused station
    QString kind;   ///< one of kMappingKinds: "base", "protein", "topping", "sauce"

    bool operator==(const ManualStation &) const = default;
};

struct ManualEntry {
    static constexpr int kStations = 19;

    std::vector<ManualStation> stations = std::vector<ManualStation>(kStations);
    QStringList sauces;     ///< served in cups, not from a station
    double floor_g = 0.0;   ///< one weight floor for every bowl; 0 for none

    bool empty() const;
};

/// Kept in the app's settings, under manual/, so the window and the command line
/// read the same stations.
ManualEntry load_manual_entry();
void save_manual_entry(const ManualEntry &entry);

/// Every ingredient and sauce any loaded brand serves, sorted, without duplicates.
QStringList ingredient_catalog(const std::vector<Menu> &menus);
/// Sauces alone, as the brands list them.
QStringList sauce_catalog(const std::vector<Menu> &menus);

/// What a name serves as: the kind most brands give it, or a guess from the name for
/// one no brand carries.
QString catalog_kind(const std::vector<Menu> &menus, const QString &name);

/// Builds the brand from the stations, in station order. `menus` are the loaded
/// brands; any Manual Entry among them is ignored.
Menu manual_entry_menu(const std::vector<Menu> &menus, const ManualEntry &entry,
                       const PieceHeights *pieces = nullptr);

}  // namespace bowlfill
