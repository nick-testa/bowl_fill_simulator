#include "text.hh"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>

namespace bowlfill
{
namespace
{

bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// True when the digits past the ones kept are a 5 and then only zeros: the value sits
/// exactly halfway between two roundings.
bool is_tie(std::string_view rest)
{
    return !rest.empty() && rest.front() == '5' && rest.find_first_not_of('0', 1) == std::string_view::npos;
}

/// One step away from zero when `value` is an exact tie, according to `exact` (its
/// digits, far past where it will be rounded, from position `kept` on). std::format
/// rounds an exact tie to even, where the reports have always rounded it away from zero
/// (474.5 g is "475"), as Qt does; moving off the tie makes std::format agree.
double break_tie(double value, std::string_view rest)
{
    return is_tie(rest) ? std::nextafter(value, value < 0 ? -INFINITY : INFINITY) : value;
}

/// Thirty digits past the last one kept is enough to tell an exact tie from a value
/// merely close to one, at the magnitudes the core prints.
constexpr int kTieDigits = 30;

} // namespace

std::string trim(std::string_view s)
{
    size_t b = 0, e = s.size();
    while (b < e && is_space(s[b]))
    {
        ++b;
    }
    while (e > b && is_space(s[e - 1]))
    {
        --e;
    }
    return std::string(s.substr(b, e - b));
}

std::string to_lower(std::string_view s)
{
    std::string out(s);
    for (char &c : out)
    {
        c = lower(c);
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (lower(a[i]) != lower(b[i]))
        {
            return false;
        }
    }
    return true;
}

bool istarts_with(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

bool icontains(std::string_view s, std::string_view needle)
{
    return to_lower(s).find(to_lower(needle)) != std::string::npos;
}

bool iless(std::string_view a, std::string_view b)
{
    return std::lexicographical_compare(
        a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) { return lower(x) < lower(y); });
}

bool contains(const std::vector<std::string> &list, std::string_view item)
{
    return std::find(list.begin(), list.end(), item) != list.end();
}

void append_unique(std::vector<std::string> &list, const std::string &item)
{
    if (!contains(list, item))
    {
        list.push_back(item);
    }
}

void remove_duplicates(std::vector<std::string> &list)
{
    std::vector<std::string> out;
    for (std::string &s : list)
    {
        if (!contains(out, s))
        {
            out.push_back(std::move(s));
        }
    }
    list = std::move(out);
}

void sort_case_insensitive(std::vector<std::string> &list)
{
    std::stable_sort(list.begin(), list.end(), [](const std::string &a, const std::string &b) { return iless(a, b); });
}

std::string join(const std::vector<std::string> &list, std::string_view separator)
{
    std::string out;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (i)
        {
            out += separator;
        }
        out += list[i];
    }
    return out;
}

std::vector<std::string> split(std::string_view s, char separator)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i)
    {
        if (i == s.size() || s[i] == separator)
        {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::string replace_all(std::string s, std::string_view from, std::string_view to)
{
    if (from.empty())
    {
        return s;
    }
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
    {
        s.replace(at, from.size(), to);
    }
    return s;
}

std::optional<double> parse_double(std::string_view s)
{
    std::string t = trim(s);
    std::string_view v = t;
    if (!v.empty() && v.front() == '+')
    {
        v.remove_prefix(1);
    }
    if (v.empty())
    {
        return std::nullopt;
    }
    double out = 0.0;
    const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    if (ec != std::errc() || end != v.data() + v.size())
    {
        return std::nullopt;
    }
    return out;
}

std::optional<int> parse_int(std::string_view s)
{
    std::string t = trim(s);
    std::string_view v = t;
    if (!v.empty() && v.front() == '+')
    {
        v.remove_prefix(1);
    }
    if (v.empty())
    {
        return std::nullopt;
    }
    int out = 0;
    const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    if (ec != std::errc() || end != v.data() + v.size())
    {
        return std::nullopt;
    }
    return out;
}

std::string fixed(double value, int decimals)
{
    if (value == 0.0)
    {
        value = 0.0; // -0.0 prints as "0", not "-0"
    }
    // Most values are nowhere near a tie; only look closely at those that might be.
    const double scaled = std::fabs(value) * std::pow(10.0, decimals);
    if (std::fabs(scaled - std::floor(scaled) - 0.5) < 1e-3)
    {
        const std::string exact = std::format("{:.{}f}", std::fabs(value), decimals + kTieDigits);
        value = break_tie(value, std::string_view(exact).substr(exact.find('.') + 1 + decimals));
    }
    return std::format("{:.{}f}", value, decimals);
}

std::string general(double value)
{
    if (value == 0.0)
    {
        value = 0.0;
    }
    // Six significant figures: the digits after the sixth decide the rounding.
    const std::string exact = std::format("{:.{}e}", std::fabs(value), 5 + kTieDigits);
    const size_t e = exact.find('e');
    value = break_tie(value, std::string_view(exact).substr(7, e - 7));
    return std::format("{:.6g}", value);
}

std::vector<std::string> split_csv(std::string_view line)
{
    std::vector<std::string> out;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (quoted)
        {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"')
            {
                field += '"';
                ++i;
            }
            else if (c == '"')
            {
                quoted = false;
            }
            else
            {
                field += c;
            }
        }
        else if (c == '"')
        {
            quoted = true;
        }
        else if (c == ',')
        {
            out.push_back(std::move(field));
            field.clear();
        }
        else
        {
            field += c;
        }
    }
    out.push_back(std::move(field));
    return out;
}

std::string csv_escape(std::string_view s)
{
    if (s.find(',') == std::string_view::npos && s.find('"') == std::string_view::npos)
    {
        return std::string(s);
    }
    return "\"" + replace_all(std::string(s), "\"", "\"\"") + "\"";
}

bool read_lines(const std::string &path, std::vector<std::string> &lines)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return false;
    }
    lines.clear();
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (lines.empty() && line.starts_with("\xEF\xBB\xBF"))
        {
            line.erase(0, 3);
        }
        lines.push_back(std::move(line));
    }
    return true;
}

bool write_text(const std::string &path, std::string_view text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }
    out << text;
    return static_cast<bool>(out);
}

} // namespace bowlfill
