#pragma once

#include "core/manual_entry.hh"

namespace bowlfill {

/// Kept in the app's settings, under manual/, so the window and the command line
/// read the same stations.
ManualEntry load_manual_entry();
void save_manual_entry(const ManualEntry &entry);

}  // namespace bowlfill
