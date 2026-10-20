#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bowlfill
{

///
/// The few string operations the core needs, on UTF-8 std::string. Ingredient names
/// and file headers are ASCII in practice, so case folding is ASCII-only: a byte
/// outside A-Z is left as it is on both sides of a comparison.
///

/// Leading and trailing whitespace removed.
std::string trim(std::string_view s);

std::string to_lower(std::string_view s);

bool iequals(std::string_view a, std::string_view b);

bool istarts_with(std::string_view s, std::string_view prefix);

/// True when `s` contains `needle`, ignoring case.
bool icontains(std::string_view s, std::string_view needle);

/// Less-than ignoring case, for sorting names the way a reader expects.
bool iless(std::string_view a, std::string_view b);

bool contains(const std::vector<std::string> &list, std::string_view item);

/// Appends `item` unless the list already holds it.
void append_unique(std::vector<std::string> &list, const std::string &item);

/// Drops repeats, keeping each item's first occurrence in place.
void remove_duplicates(std::vector<std::string> &list);

/// Sorts ignoring case; items equal but for case keep their order.
void sort_case_insensitive(std::vector<std::string> &list);

std::string join(const std::vector<std::string> &list, std::string_view separator);

/// Splits on `separator`. Empty pieces are kept, so "a,,b" is three pieces.
std::vector<std::string> split(std::string_view s, char separator);

/// Every occurrence of `from` replaced by `to`.
std::string replace_all(std::string s, std::string_view from, std::string_view to);

/// A whole string read as a number, surrounding whitespace allowed. Empty when
/// anything else is in it.
std::optional<double> parse_double(std::string_view s);

std::optional<int> parse_int(std::string_view s);

/// `value` with exactly `decimals` places: 12.345 at 1 is "12.3". An exact tie rounds
/// away from zero (474.5 at 0 is "475", 0.125 at 2 is "0.13"), and -0.0 prints as "0",
/// as the reports always have.
std::string fixed(double value, int decimals);

/// `value` in the shorter of fixed or exponent form, six significant figures, as
/// printf's %g writes it: 455 is "455", 0.5 is "0.5". Ties and -0.0 as for fixed().
std::string general(double value);

/// One CSV record. Names carry commas ("Guajillo / Cumin Chicken, [cooked]"), so
/// quoted fields and doubled quotes are honoured.
std::vector<std::string> split_csv(std::string_view line);

/// Quotes a CSV field when it holds a comma or a quote.
std::string csv_escape(std::string_view s);

/// Reads a text file as lines, without line endings and without a leading UTF-8 byte
/// order mark (spreadsheet exports often leave one). False when it cannot be opened.
bool read_lines(const std::string &path, std::vector<std::string> &lines);

/// Writes `text` to `path`, replacing it. False when it cannot be written.
bool write_text(const std::string &path, std::string_view text);
/// One CSV field, quoted only when it has to be (a comma, a quote, or edge spaces).
std::string csv_field(const std::string &s);

} // namespace bowlfill
