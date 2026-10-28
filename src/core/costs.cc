#include "costs.hh"

#include "text.hh"

#include <algorithm>

namespace bowlfill
{

bool CostTable::load_csv(const std::string &path, std::string *error)
{
    std::vector<std::string> lines;
    if (!read_lines(path, lines))
    {
        if (error)
        {
            *error = "could not open " + path;
        }
        return false;
    }
    per_kg_.clear();
    source_.clear();
    fallback_.clear();

    bool seen_header = false;
    for (const std::string &raw_line : lines)
    {
        const std::string line = trim(raw_line);
        if (line.empty() || line.starts_with('#'))
        {
            continue;
        }
        const std::vector<std::string> cells = split(line, ',');
        if (cells.size() < 2)
        {
            continue;
        }
        const std::string name = trim(cells[0]);
        if (!seen_header && iequals(name, "ingredient"))
        {
            seen_header = true;
            continue;
        }
        const std::optional<double> per_kg = parse_double(cells[1]);
        if (!per_kg || *per_kg < 0)
        {
            continue;
        }

        // "*base" / "*protein" / "*topping" set the per-kind fallback.
        if (name.starts_with('*'))
        {
            const std::string kind = to_lower(name.substr(1));
            if (kind == "base")
            {
                fallback_[Kind::Base] = *per_kg;
            }
            else if (kind == "protein")
            {
                fallback_[Kind::Protein] = *per_kg;
            }
            else if (kind == "topping")
            {
                fallback_[Kind::Topping] = *per_kg;
            }
            continue;
        }
        per_kg_[name] = *per_kg;
        // An optional third column says where the price came from; older files put a
        // free-text note there, which reads as no source.
        const std::string source = cells.size() > 2 ? to_lower(trim(cells[2])) : std::string();
        source_[name] = source == "kitchenos" ? CostSource::KitchenOs
                        : source == "estimate" ? CostSource::Estimate
                                               : CostSource::Placeholder;
    }
    if (per_kg_.empty() && error)
    {
        *error = "no usable rows";
    }
    return !per_kg_.empty();
}

double CostTable::cost_of(const std::string &name, Kind kind, double grams, bool *known) const
{
    auto it = per_kg_.find(name);
    if (it != per_kg_.end())
    {
        if (known)
        {
            *known = true;
        }
        return grams / 1000.0 * it->second;
    }
    if (known)
    {
        *known = false;
    }
    auto f = fallback_.find(kind);
    return f == fallback_.end() ? 0.0 : grams / 1000.0 * f->second;
}

std::optional<double> CostTable::per_kg(const std::string &name) const
{
    auto it = per_kg_.find(name);
    if (it == per_kg_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

CostSource CostTable::source_of(const std::string &name) const
{
    auto it = source_.find(name);
    return it == source_.end() ? CostSource::Placeholder : it->second;
}

std::string to_string(CostSource source)
{
    switch (source)
    {
    case CostSource::KitchenOs:
        return "KitchenOS";
    case CostSource::Estimate:
        return "estimate";
    case CostSource::Placeholder:
        return "placeholder";
    }
    return "placeholder";
}

bool BowlCost::complete() const
{
    return std::all_of(lines.begin(), lines.end(), [](const BowlCostLine &l) { return l.known; });
}

bool BowlCost::all_kitchenos() const
{
    return std::all_of(lines.begin(), lines.end(),
                       [](const BowlCostLine &l) { return l.known && l.source == CostSource::KitchenOs; });
}

double BowlCost::kitchenos_share() const
{
    if (total <= 0)
    {
        return 0.0;
    }
    double kos = 0.0;
    for (const BowlCostLine &l : lines)
    {
        if (l.known && l.source == CostSource::KitchenOs)
        {
            kos += l.cost;
        }
    }
    return kos / total;
}

BowlCost cost_bowl(const std::vector<BowlItem> &items, const CostTable &costs)
{
    BowlCost out;
    for (const BowlItem &it : items)
    {
        BowlCostLine l;
        l.name = it.name;
        l.kind = it.kind;
        l.grams = it.final_g;
        l.cost = costs.cost_of(it.name, it.kind, it.final_g, &l.known);
        l.source = costs.source_of(it.name);
        out.total += l.cost;
        out.lines.push_back(std::move(l));
    }
    return out;
}

} // namespace bowlfill
