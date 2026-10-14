#include "manual_entry_settings.hh"

#include <QSettings>
#include <QStringList>

#include <algorithm>

namespace bowlfill {

ManualEntry load_manual_entry()
{
    QSettings prefs;
    ManualEntry entry;
    const int n = prefs.beginReadArray("manual/stations");
    for (int i = 0; i < std::min(n, ManualEntry::kStations); ++i) {
        prefs.setArrayIndex(i);
        ManualStation &s = entry.stations[i];
        s.name = prefs.value("name").toString().trimmed().toStdString();
        s.kind = prefs.value("kind").toString().toStdString();
        if (!contains(kMappingKinds, s.kind)) s.kind = s.name.empty() ? std::string() : "topping";
    }
    prefs.endArray();
    for (const QString &sauce : prefs.value("manual/sauces").toStringList())
        entry.sauces.push_back(sauce.toStdString());
    entry.floor_g = prefs.value("manual/floor_g", 0.0).toDouble();
    return entry;
}

void save_manual_entry(const ManualEntry &entry)
{
    QSettings prefs;
    prefs.remove("manual/stations");
    prefs.beginWriteArray("manual/stations", ManualEntry::kStations);
    for (int i = 0; i < ManualEntry::kStations; ++i) {
        prefs.setArrayIndex(i);
        const ManualStation s = i < static_cast<int>(entry.stations.size())
                                    ? entry.stations[i] : ManualStation{};
        prefs.setValue("name", QString::fromStdString(s.name));
        prefs.setValue("kind", QString::fromStdString(s.kind));
    }
    prefs.endArray();
    QStringList sauces;
    for (const std::string &sauce : entry.sauces) sauces << QString::fromStdString(sauce);
    prefs.setValue("manual/sauces", sauces);
    prefs.setValue("manual/floor_g", entry.floor_g);
}

}  // namespace bowlfill
