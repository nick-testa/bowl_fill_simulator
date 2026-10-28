#include "bowl_builder.hh"

#include "bowl_diagram.hh"
#include "bowl_editor.hh"
#include "chip_picker.hh"
#include "qt_text.hh"
#include "theme.hh"
#include "units.hh"

#include "core/manual_entry.hh"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace bowlfill {

namespace space = theme::space;

namespace {

/// The golden ratio's two parts, as layout stretch factors: 61.8 to 38.2.
constexpr int kMajor = 618, kMinor = 382;
/// The column of panels stays readable however the window is sized: never cramped,
/// never a stretched form.
constexpr int kMinColumn = 400, kMaxColumn = 640;

/// A portion for an ingredient whose menu sets none, so it still shows in the bowl.
constexpr double kFallbackPortionG = 50.0;

QLabel *make_label(const QString &text, const char *role)
{
    auto *l = new QLabel(text);
    l->setObjectName(role);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    return l;
}

QLabel *section_label(const QString &text)
{
    auto *l = new QLabel(text.toUpper());
    l->setObjectName("section");
    return l;
}

QWidget *field(const QString &caption, QWidget *w)
{
    auto *box = new QWidget;
    box->setObjectName("clear");
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(space::xs);
    auto *l = new QLabel(caption);
    l->setObjectName("label");
    v->addWidget(l);
    v->addWidget(w);
    return box;
}

QFrame *titled_panel(const QString &title, QVBoxLayout *&body)
{
    auto *panel = new QFrame;
    panel->setObjectName("panel");
    body = new QVBoxLayout(panel);
    body->setContentsMargins(space::lg, space::lg, space::lg, space::lg);
    body->setSpacing(space::md);
    if (!title.isEmpty()) body->addWidget(make_label(title, "heading"));
    return panel;
}

/// A titled panel whose contents scroll under a title that stays put, for a panel given a
/// share of the height rather than the height of what is in it.
QFrame *scrolling_panel(const QString &title, QVBoxLayout *&body)
{
    auto *panel = new QFrame;
    panel->setObjectName("panel");
    auto *outer = new QVBoxLayout(panel);
    outer->setContentsMargins(space::lg, space::lg, space::xs, space::lg);
    outer->setSpacing(space::md);
    auto *heading = make_label(title, "heading");
    heading->setContentsMargins(0, 0, space::md, 0);
    outer->addWidget(heading);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("panelScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *host = new QWidget;
    host->setObjectName("clear");
    body = new QVBoxLayout(host);
    body->setContentsMargins(0, 0, space::md, 0);
    body->setSpacing(space::md);
    scroll->setWidget(host);
    outer->addWidget(scroll, 1);
    return panel;
}

QComboBox *make_combo()
{
    auto *c = new QComboBox;
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(6);
    c->setFixedHeight(32);
    return c;
}

QString capitalised_kind(Kind kind)
{
    QString s = qs(to_string(kind));
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

/// A big number over its caption, for the bowl's totals.
QWidget *stat(const QString &caption, QLabel *&value)
{
    auto *box = new QWidget;
    box->setObjectName("clear");
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto *key = make_label(caption, "statKey");
    key->setWordWrap(false);
    value = new QLabel(QString::fromUtf8("—"));
    value->setObjectName("title");
    v->addWidget(key);
    v->addWidget(value);
    return box;
}

}  // namespace

BowlBuilder::BowlBuilder(BuilderContext context, QWidget *parent)
    : QWidget(parent), ctx_(std::move(context))
{
    setObjectName("clear");
    // Golden proportions throughout: the column of panels and the bowl share the width
    // 1 : φ, and the column's two panels share its height φ : 1.
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(space::lg);

    // ---- left: what goes in the bowl, over what it comes to -------------------------
    auto *column = new QWidget;
    column->setObjectName("clear");
    column->setMinimumWidth(kMinColumn);
    column->setMaximumWidth(kMaxColumn);
    column->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *cv = new QVBoxLayout(column);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(space::lg);
    QVBoxLayout *v = nullptr, *tv = nullptr;
    QFrame *build_panel = scrolling_panel("Build a bowl", v);
    QFrame *bowl_panel = scrolling_panel("This bowl", tv);
    for (QFrame *f : {build_panel, bowl_panel}) f->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    cv->addWidget(build_panel, kMajor);
    cv->addWidget(bowl_panel, kMinor);
    h->addWidget(column, kMinor);

    v->addWidget(make_label("Pick a menu and what goes in the bowl, then drag each layer to "
                            "how much of the bowl it should fill.", "note"));
    brand_ = make_combo();
    brand_->setToolTip("The menu whose ingredients the bowl is built from. Manual Entry is "
                       "whatever is loaded on the robot (Settings → Robot stations).");
    connect(brand_, &QComboBox::currentIndexChanged, this, &BowlBuilder::on_brand_changed);
    v->addWidget(field("Menu", brand_));
    start_ = make_combo();
    start_->setToolTip("Start from one of the menu's bowls, or build your own.");
    connect(start_, &QComboBox::activated, this, &BowlBuilder::on_start_changed);
    v->addWidget(field("Start from", start_));
    start_note_ = make_label("", "note");
    v->addWidget(start_note_);

    auto picker = [&](const QString &caption, const QString &noun, ChipPicker *&out, int kind) {
        v->addSpacing(space::xs);
        v->addWidget(section_label(caption));
        out = new ChipPicker(noun);
        out->set_swatches([kind](const QString &name) { return theme::food_colour(name, kind); });
        connect(out, &ChipPicker::changed, this, &BowlBuilder::on_picks_changed);
        v->addWidget(out);
    };
    picker("Bases · up to 2", "base", bases_, 0);
    bases_->set_limit(2);
    picker("Proteins · up to 2", "protein", proteins_, 1);
    proteins_->set_limit(2);
    picker("Toppings", "topping", toppings_, 2);
    picker(QString("Sauces · up to %1 · one cup each").arg(SauceCups::kMax), "sauce",
           sauces_, 2);
    sauces_->set_limit(SauceCups::kMax);

    // ---- centre: the bowl, cut away to its left side, to drag the layers in -------
    QVBoxLayout *ev = nullptr;
    QFrame *editor_panel = titled_panel("Fill the bowl", ev);
    auto *hint = make_label("Drag a tab to set how much of the bowl each ingredient fills.",
                            "lead");
    hint->setToolTip("The left side of the bowl, cut away and to scale, with each ingredient a "
                     "layer in the order it is dispensed. Dragging a tab, or the top of a "
                     "layer, moves that layer's top; the layers above ride along.");
    ev->addWidget(hint);
    editor_ = new BowlEditor;
    connect(editor_, &BowlEditor::edge_moved, this, &BowlBuilder::on_edge_moved);
    ev->addWidget(editor_, 1);
    editor_panel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    h->addWidget(editor_panel, kMajor);

    // ---- what the bowl comes to: the totals, then a line per ingredient -------------
    auto *stats = new QHBoxLayout;
    stats->setSpacing(space::xl);
    stats->addWidget(stat("Bowl fill", fill_value_));
    stats->addWidget(stat("Weight", weight_value_));
    stats->addWidget(stat("Volume", volume_value_));
    stats->addWidget(stat("Food cost", cost_value_));
    stats->addStretch(1);
    tv->addLayout(stats);
    fill_note_ = make_label("", "note");
    tv->addWidget(fill_note_);
    risk_note_ = make_label("", "lead");
    tv->addWidget(risk_note_);
    // Set the proportions however looks right, then scale them up (or down) together.
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(space::sm);
    to_max_ = new QPushButton;
    to_max_->setObjectName("primary");
    to_max_->setToolTip("Keep these proportions and scale every ingredient together until the "
                        "bowl is full: at its max fill.");
    connect(to_max_, &QPushButton::clicked, this, [this] {
        if (const Menu *m = menu()) scale_to(ctx_.max_fill(*m));
    });
    auto *reset = new QPushButton("Reset to menu portions");
    reset->setObjectName("quiet");
    reset->setToolTip("Go back to how the bowl started: a recipe as the menu writes it, or "
                      "your own bowl at the menu's proportions, filled to the max.");
    connect(reset, &QPushButton::clicked, this, [this] {
        const Menu *m = menu();
        if (!m) return;
        portion_shares(pinned_, as_written_ ? std::nullopt : std::optional(ctx_.max_fill(*m)));
        recompute();
    });
    buttons->addWidget(to_max_);
    buttons->addWidget(reset);
    buttons->addStretch(1);
    tv->addLayout(buttons);

    tv->addSpacing(space::xs);
    tv->addWidget(section_label("Ingredients · % of the bowl"));
    rows_ = new QVBoxLayout;
    rows_->setSpacing(space::sm);
    tv->addLayout(rows_);
    empty_note_ = make_label("Add a base, a protein or a topping to start the bowl.", "lead");
    tv->addWidget(empty_note_);
    sauce_note_ = make_label("", "note");
    tv->addWidget(sauce_note_);
    tv->addStretch(1);
    v->addStretch(1);

    reload_menus();
}

const Menu *BowlBuilder::menu() const
{
    const int i = brand_->currentIndex();
    return ctx_.menus && i >= 0 && i < static_cast<int>(ctx_.menus->size()) ? &(*ctx_.menus)[i]
                                                                           : nullptr;
}

void BowlBuilder::reload_menus()
{
    // The bowl is kept when its brand is still there; its picks are filtered to what the
    // reloaded menu still offers by the pickers themselves.
    const QString keep = brand_->currentIndex() >= 0 ? brand_->currentData().toString()
                                                     : QSettings().value("builder/brand").toString();
    const bool same_brand = brand_->currentIndex() >= 0;
    loading_ = true;
    brand_->clear();
    for (const Menu &m : *ctx_.menus) brand_->addItem(qs(m.brand), qs(m.brand));
    const int i = brand_->findData(keep);
    brand_->setCurrentIndex(i >= 0 ? i : 0);
    loading_ = false;
    if (same_brand && i >= 0) {
        // Names the menu no longer offers drop out of the pickers, and so of the bowl.
        const Menu *m = menu();
        bases_->set_options(qsl(m->names_of(Kind::Base)));
        proteins_->set_options(qsl(m->names_of(Kind::Protein)));
        toppings_->set_options(qsl(m->names_of(Kind::Topping)));
        sauces_->set_options(qsl(m->sauces));
        on_picks_changed();
        return;
    }
    on_brand_changed();
}

void BowlBuilder::select_brand(const QString &brand, const QString &recipe)
{
    if (const int i = brand_->findData(brand); i >= 0) brand_->setCurrentIndex(i);
    if (const int i = start_->findData(recipe); !recipe.isEmpty() && i >= 0) {
        start_->setCurrentIndex(i);
        on_start_changed();
    }
}

void BowlBuilder::refresh()
{
    if (!loading_) recompute();
}

void BowlBuilder::on_brand_changed()
{
    if (loading_) return;
    const Menu *m = menu();
    if (!m) return;
    QSettings().setValue("builder/brand", qs(m->brand));

    loading_ = true;
    bases_->set_options(qsl(m->names_of(Kind::Base)));
    proteins_->set_options(qsl(m->names_of(Kind::Protein)));
    toppings_->set_options(qsl(m->names_of(Kind::Topping)));
    sauces_->set_options(qsl(m->sauces));
    start_->clear();
    start_->addItem("Build your own", QString());
    // Manual Entry's one recipe is the empty build-your-own shell.
    const bool manual = m->brand == kManualEntryBrand;
    for (const Recipe &r : m->recipes)
        if (!manual || !r.customer_built) start_->addItem(qs(r.name), qs(r.name));
    start_->setCurrentIndex(0);
    loading_ = false;
    on_start_changed();
}

void BowlBuilder::on_start_changed()
{
    const Menu *m = menu();
    if (!m) return;
    const QString want = start_->currentData().toString();
    const Recipe *rec = nullptr;
    for (const Recipe &r : m->recipes)
        if (qs(r.name) == want) rec = &r;

    QStringList bases, proteins, toppings, sauces;
    std::map<QString, double> pinned;
    if (!rec) {
        // One of each, as a customer starts: the first base and protein, one sauce.
        const QStringList b = qsl(m->names_of(Kind::Base)), p = qsl(m->names_of(Kind::Protein));
        if (!b.isEmpty()) bases << b.front();
        if (!p.isEmpty()) proteins << p.front();
        if (!m->sauces.empty()) sauces << qs(m->sauces.front());
        start_note_->setText(m->ingredients.empty() && m->sauces.empty()
                                 ? QString("<span style='color:%1'>This menu has no ingredients "
                                           "yet.</span>").arg(theme::palette().over.name())
                                 : QString("A base, a protein and a sauce to start. Add or "
                                           "remove anything."));
    } else {
        for (const RecipeItem &ri : rec->items) {
            const Ingredient *ing = m->find(ri.name);
            if (!ing) continue;
            if (ri.grams > 0) pinned[qs(ri.name)] = ri.grams;
            (ing->kind == Kind::Base ? bases : ing->kind == Kind::Protein ? proteins : toppings)
                << qs(ri.name);
        }
        sauces = qsl(m->sauces_in(*rec)).mid(0, SauceCups::kMax);
        // Most templates pin only a protein and leave the rest to the customer, so a
        // bowl without a base or a sauce gets the build-your-own one.
        QStringList added;
        const QStringList b = qsl(m->names_of(Kind::Base));
        if (bases.isEmpty() && !b.isEmpty()) added << (bases << b.front()).back();
        if (sauces.isEmpty() && !m->sauces.empty()) added << (sauces << qs(m->sauces.front())).back();
        QStringList kept;
        for (const auto &[name, grams] : pinned) kept << QString("%1 %2 g").arg(name).arg(std::round(grams));
        QString note = kept.isEmpty()
                           ? QString("The recipe sets no weights, so its ingredients start at their "
                                     "menu portions.")
                           : QString("As the recipe writes it: %1.").arg(kept.join(", "));
        if (!kept.isEmpty()
            && static_cast<int>(pinned.size()) < bases.size() + proteins.size() + toppings.size()
                                                     - (added.isEmpty() || !b.contains(added.front()) ? 0 : 1))
            note += " Ingredients it gives no weight start at their menu portions.";
        if (!added.isEmpty()) {
            QStringList named;
            for (const QString &n : added) {
                const Ingredient *ing = m->find(ss(n));
                named << (ing ? QString("%1 (%2 g)").arg(n).arg(std::round(ing->full_portion())) : n);
            }
            note += QString(" It leaves %1 to the customer, so %2 %3 added at the menu's portion.")
                        .arg(added.size() == 2 ? "the base and sauce"
                             : b.contains(added.front()) ? "the base" : "the sauce")
                        .arg(named.join(" and ")).arg(added.size() == 1 ? "is" : "are");
        }
        note += " Scale to a full bowl keeps these proportions.";
        start_note_->setText(note);
    }
    loading_ = true;
    bases_->set_picked(bases.mid(0, 2));
    // A preconfigured bowl can carry three proteins; the cap floats up to fit it.
    proteins_->set_limit(std::max(2, static_cast<int>(proteins.size())));
    proteins_->set_picked(proteins);
    toppings_->set_picked(toppings);
    sauces_->set_picked(sauces);
    loading_ = false;
    // A named recipe starts as the menu writes it; building your own starts full.
    pinned_ = pinned;
    as_written_ = rec != nullptr;
    portion_shares(pinned_, as_written_ ? std::nullopt : std::optional(ctx_.max_fill(*m)));
    rebuild_rows();
    recompute();
}

void BowlBuilder::on_picks_changed()
{
    if (loading_) return;
    const Menu *m = menu();
    if (!m) return;
    const QStringList now = dispensed();
    // Gone: dropped. New: in at its menu portion, the rest left as they were.
    for (auto it = share_.begin(); it != share_.end();)
        it = now.contains(it->first) ? std::next(it) : share_.erase(it);
    const double room = room_oz();
    for (const QString &name : now) {
        if (share_.count(name)) continue;
        const Ingredient *ing = m->find(ss(name));
        if (!ing || room <= 0) continue;
        const BowlItem it = ctx_.make_item(*ing);
        share_[name] = it.volume_oz(menu_portion(name, {})) / room;
    }
    if (proteins_->picked().size() <= 2) proteins_->set_limit(2);
    rebuild_rows();
    recompute();
}

QStringList BowlBuilder::dispensed() const
{
    return bases_->picked() + proteins_->picked() + toppings_->picked();
}

double BowlBuilder::menu_portion(const QString &name, const std::map<QString, double> &pinned) const
{
    if (const auto pin = pinned.find(name); pin != pinned.end()) return pin->second;
    const Menu *m = menu();
    const Ingredient *ing = m ? m->find(ss(name)) : nullptr;
    if (!ing) return 0.0;
    const double g = ing->kind == Kind::Base
                         ? split_base_start(*ing, true, bases_->picked().size() == 2)
                         : ing->full_portion();
    return g > 0 ? g : kFallbackPortionG;
}

void BowlBuilder::portion_shares(const std::map<QString, double> &pinned, std::optional<double> total)
{
    // Everything is in the bowl, for which chunks the room makes way for.
    const QStringList names = dispensed();
    share_.clear();
    for (const QString &n : names) share_[n] = 1.0;
    const std::vector<BowlItem> its = items();
    const SimSettings s = bowl_settings(its);
    const double room = s.food_capacity_oz();
    std::vector<double> g;
    for (const QString &n : names) g.push_back(menu_portion(n, pinned));
    if (room <= 0 || its.size() != static_cast<size_t>(names.size())) {
        for (const QString &n : names) share_[n] = 0.0;
        return;
    }
    // Pinned weights stay as the recipe has them; the rest keep the menu's proportions
    // and fill what is left up to `total`, or stay at their menu portions without one. What a base takes depends on what lies on it,
    // so the shares are settled over a few rounds with the pinned grams held.
    for (int round = 0; round < 6; ++round) {
        const Frame f = bowl_frame(its, g, s);
        double fixed = 0.0, free = 0.0;
        for (int i = 0; i < names.size(); ++i)
            (pinned.count(names[i]) ? fixed : free) += f.per_item_oz[i] / room;
        // Pinned alone at or past the max: the rest stay at their menu portions.
        const double scale = total && free > 0 && *total > fixed ? (*total - fixed) / free : 1.0;
        std::vector<double> target;
        for (int i = 0; i < names.size(); ++i) {
            const double share = f.per_item_oz[i] / room * (pinned.count(names[i]) ? 1.0 : scale);
            share_[names[i]] = share;
            target.push_back(share * room);
        }
        g = grams_for_volumes(its, target, s);
        for (int i = 0; i < names.size(); ++i)
            if (const auto pin = pinned.find(names[i]); pin != pinned.end()) g[i] = pin->second;
    }
}

void BowlBuilder::set_share(const QString &name, double share)
{
    share_[name] = std::clamp(share, 0.0, 1.0);
    recompute();
}

void BowlBuilder::scale_to(double total)
{
    double sum = 0.0;
    for (const auto &[name, share] : share_) sum += share;
    if (sum <= 0) return;
    for (auto &[name, share] : share_) share *= total / sum;
    recompute();
}

std::vector<BowlItem> BowlBuilder::items() const
{
    std::vector<BowlItem> out;
    const Menu *m = menu();
    if (!m) return out;
    for (const QString &name : dispensed())
        if (const Ingredient *ing = m->find(ss(name))) {
            BowlItem it = ctx_.make_item(*ing);
            // In the bowl or not, for which chunks count against the lid.
            const auto s = share_.find(name);
            it.start_g = s != share_.end() && s->second > 0 ? 1.0 : 0.0;
            out.push_back(it);
        }
    return out;
}

SimSettings BowlBuilder::bowl_settings(const std::vector<BowlItem> &its) const
{
    SimSettings s = ctx_.settings();
    s.floor_g = 0.0;   // nothing ramps: the bowl is what the chef set
    s.sauce.cups = std::min(static_cast<int>(sauces_->picked().size()), SauceCups::kMax);
    s.chunk_height_mm = tallest_chunk_mm(its);
    return s;
}

double BowlBuilder::room_oz() const
{
    return bowl_settings(items()).food_capacity_oz();
}

void BowlBuilder::rebuild_rows()
{
    row_widgets_.clear();
    while (QLayoutItem *item = rows_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const QStringList names = dispensed();
    empty_note_->setVisible(names.isEmpty());
    // One line an ingredient: its colour and name, its share (typed), what that weighs
    // and takes up, and its cost. Fixed widths keep the columns aligned row to row.
    for (const QString &name : names) {
        Row r;
        r.name = name;
        r.box = new QWidget;
        r.box->setObjectName("clear");
        auto *line = new QHBoxLayout(r.box);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(space::sm);
        r.swatch = new QLabel;
        r.swatch->setFixedSize(12, 12);
        r.label = new QLabel(name);
        r.label->setWordWrap(true);
        r.label->setMinimumWidth(80);
        r.pct = new QDoubleSpinBox;
        r.pct->setRange(0, 100);
        r.pct->setDecimals(1);
        r.pct->setSingleStep(1);
        r.pct->setSuffix(" %");
        r.pct->setKeyboardTracking(false);
        r.pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        r.pct->setFixedSize(84, 30);
        r.pct->setToolTip("How much of the bowl's food room this fills. Type a value, or drag "
                          "its layer in the bowl.");
        theme::bare_spin(r.pct);
        r.amount = new QLabel;
        r.amount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        r.amount->setFixedWidth(118);
        r.cost = new QLabel;
        r.cost->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        r.cost->setFixedWidth(52);
        connect(r.pct, &QDoubleSpinBox::valueChanged, this,
                [this, name](double v) { set_share(name, v / 100.0); });
        line->addWidget(r.swatch, 0, Qt::AlignVCenter);
        line->addWidget(r.label, 1);
        line->addWidget(r.pct);
        line->addWidget(r.amount);
        line->addWidget(r.cost);
        rows_->addWidget(r.box);
        row_widgets_.push_back(r);
    }
}

void BowlBuilder::on_edge_moved(int index, double top_oz)
{
    if (index < 0 || index >= static_cast<int>(row_widgets_.size()) || last_room_ <= 0) return;
    double below = 0.0;
    for (int i = 0; i < index && i < static_cast<int>(last_oz_.size()); ++i) below += last_oz_[i];
    set_share(row_widgets_[index].name, std::max(0.0, top_oz - below) / last_room_);
}

void BowlBuilder::recompute()
{
    const Menu *m = menu();
    if (loading_ || !m) return;
    const theme::Palette &pal = theme::palette();

    // Shares to volumes, volumes to grams through each curve with what lies on it.
    std::vector<BowlItem> its = items();
    const SimSettings s = bowl_settings(its);
    const double room = s.food_capacity_oz();
    const QStringList names = dispensed();
    std::vector<double> target;
    for (const QString &n : names) target.push_back(share_.count(n) ? share_.at(n) * room : 0.0);
    const std::vector<double> grams = grams_for_volumes(its, target, s);
    for (size_t i = 0; i < its.size() && i < grams.size(); ++i) its[i].start_g = grams[i];
    const SimResult r = simulate(its, s);
    const Frame &f = r.first();
    const double max = ctx_.max_fill(*m);

    const std::vector<QColor> colours = band_colours(r.items, f.per_item_oz);
    const BowlCost cost = cost_bowl(r.items, *ctx_.costs);

    // ---- the rows -----------------------------------------------------------
    std::vector<BowlEditor::Layer> layers;
    for (size_t i = 0; i < row_widgets_.size() && i < r.items.size(); ++i) {
        const Row &row = row_widgets_[i];
        const BowlItem &it = r.items[i];
        QColor colour = i < colours.size() && colours[i].isValid()
                            ? colours[i]
                            : theme::food_colour(row.name, static_cast<int>(it.kind));
        layers.push_back({row.name, colour, f.per_item_oz[i]});
        row.swatch->setStyleSheet(QString("background:%1; border-radius:3px;").arg(colour.name()));
        {
            const QSignalBlocker block(row.pct);
            const double want = share_.count(row.name) ? share_.at(row.name) : 0.0;
            if (std::fabs(row.pct->value() - want * 100) > 0.05) row.pct->setValue(want * 100);
        }
        row.amount->setText(QString("%1 g · %2").arg(std::round(it.start_g))
                                .arg(units::volume(f.per_item_oz[i], true)));
        // More than the robot will give of it: Dynamic Portioning can only start there.
        const bool past_max = it.max_g > 0 && it.start_g > it.max_g + 0.5;
        row.amount->setStyleSheet(past_max ? QString("color:%1;").arg(pal.over.name()) : QString());
        row.amount->setToolTip(past_max ? QString("More than the robot dispenses of it: this "
                                                  "menu's max is %1 g.").arg(std::round(it.max_g))
                                        : QString());
        const BowlCostLine *line = i < cost.lines.size() ? &cost.lines[i] : nullptr;
        row.cost->setText(line ? QString("$%1%2").arg(line->cost, 0, 'f', 2)
                                     .arg(line->known ? "" : " *")
                               : QString::fromUtf8("—"));
        row.cost->setToolTip(!line ? QString()
                             : !line->known
                                 ? QString("No price on file: priced at the default for a %1.")
                                       .arg(qs(to_string(it.kind)).toLower())
                                 : QString("$%1/kg (%2)")
                                       .arg(ctx_.costs->per_kg(it.name).value_or(0.0), 0, 'f', 2)
                                       .arg(qs(to_string(line->source))));
        row.label->setToolTip(QString("%1, %2 g on the menu")
                                  .arg(capitalised_kind(it.kind))
                                  .arg(std::round(menu_portion(row.name, {}))));
    }
    editor_->set_bowl(s.geometry, s.sauce, room, max * room);
    editor_->set_layers(std::move(layers));
    last_oz_ = f.per_item_oz;
    last_room_ = room;

    // ---- the totals ---------------------------------------------------------
    const double fill = room > 0 ? f.ounces / room : 0.0;
    const bool over = fill > max + 0.0005;
    fill_value_->setText(QString("%1%").arg(std::round(fill * 100)));
    to_max_->setText(QString("Scale to a full bowl (%1%)").arg(std::round(max * 100)));
    // Nothing to scale in an empty bowl, and nothing to do in one already full.
    to_max_->setEnabled(fill > 0 && std::fabs(fill - max) > 0.0005);
    fill_value_->setStyleSheet(over ? QString("color:%1;").arg(pal.over.name()) : QString());
    const QString source = ctx_.max_fill_source(*m);
    fill_note_->setText(
        over ? QString("<span style='color:%1'><b>%2% over the max</b></span> of %3% (%4). "
                       "Lower something, or scale to a full bowl.")
                   .arg(pal.over.name()).arg(std::round((fill - max) * 100))
                   .arg(std::round(max * 100)).arg(source)
             : QString("of the food room (%1). Max fill %2%, %3.")
                   .arg(units::volume(room, true)).arg(std::round(max * 100)).arg(source));
    weight_value_->setText(QString("%1 g").arg(std::round(f.grams)));
    volume_value_->setText(units::volume(f.ounces, true));
    cost_value_->setText(cost.lines.empty() ? QString::fromUtf8("—")
                                            : QString("$%1%2").arg(cost.total, 0, 'f', 2)
                                                  .arg(cost.all_kitchenos() ? "" : " *"));
    cost_value_->setToolTip(
        QString("Food cost of the bowl at these portions, from data/ingredient_costs.csv. Sauce "
                "cups are not included.%1")
            .arg(cost.all_kitchenos() || cost.lines.empty()
                     ? QString()
                     : QString("\n* %1% of it is priced from KitchenOS; the rest is an estimate.")
                           .arg(std::round(cost.kitchenos_share() * 100))));

    if (r.fill.valid && f.ounces > 0) {
        const double limit = ctx_.risk_limit();
        const double p = r.fill.p_over_room;
        const QString pct = p < 0.01 ? QString("under 1%") : QString("%1%").arg(std::round(p * 100));
        risk_note_->setText(
            QString("<span style='color:%1'>Chance a real bowl overfills: <b>%2</b></span>")
                .arg((p > limit ? pal.over : pal.ink_soft).name(), pct));
        risk_note_->setToolTip("Real bowls vary: each batch packs a little differently and the "
                               "robot misses its weights. This is the chance one of these bowls "
                               "ends up past the food room. Bowls above the limit set under "
                               "Settings → Dispense are shown in red.");
    } else {
        risk_note_->setText("");
    }

    const QStringList sauces = sauces_->picked();
    sauce_note_->setText(
        sauces.isEmpty()
            ? QString("No sauce cups.")
            : QString("Sauce cup%1: %2. Each takes room in the bowl, which is already left out "
                      "of the food room; sauce is not in the food cost.")
                  .arg(sauces.size() == 1 ? "" : "s", sauces.join(", ")));

    // ---- what Dynamic Portioning starts from ----------------------------------
    ComposedBowl c;
    c.brand = qs(m->brand);
    c.bases = bases_->picked();
    c.proteins = proteins_->picked();
    c.toppings = toppings_->picked();
    c.sauces = sauces;
    for (size_t i = 0; i < r.items.size(); ++i) c.grams[qs(r.items[i].name)] = r.items[i].start_g;
    c.revision = composed_.revision;
    if (c.brand != composed_.brand || c.bases != composed_.bases || c.proteins != composed_.proteins
        || c.toppings != composed_.toppings || c.sauces != composed_.sauces
        || c.grams != composed_.grams)
        ++c.revision;
    composed_ = std::move(c);
    emit changed();
}

}  // namespace bowlfill
