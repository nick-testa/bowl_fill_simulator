#pragma once

#include "core/menu_model.hh"

#include <QDialog>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace bowlfill {

///
/// Maps each brand's spelling of an ingredient onto a common name, and optionally
/// fixes its kind. One row per spelling a menu uses; the "Maps to" box autocompletes
/// from every name already in use, so two brands' chickens can be pointed at one name
/// without retyping it. Export and Import round-trip the same table through a CSV for
/// editing in a spreadsheet.
///
class MappingDialog : public QDialog {
    Q_OBJECT

public:
    MappingDialog(const std::vector<Menu> &menus, const NameMappings &current,
                  const PieceHeights &pieces, const MoundSlopes &slopes,
                  const RollOffAngles &rolls, const IngredientGroups &groups,
                  const QString &brand = {},
                  QWidget *parent = nullptr);

    /// What to save: only rows that differ from the automatic name or set a kind,
    /// plus any saved rows whose menu spelling no longer appears (kept, not dropped).
    NameMappings mappings() const;
    /// Piece heights by name in use: every row given an explicit height, plus saved
    /// heights for names no row maps to any more.
    PieceHeights pieces() const;
    /// Mound slopes by name in use, kept the same way as piece heights.
    MoundSlopes slopes() const;
    /// Roll-off angles by name in use, likewise.
    RollOffAngles rolls() const;
    /// Pooling groups by name in use: every row given a group other than its automatic
    /// one, plus saved groups for names no row maps to any more.
    IngredientGroups groups() const;

private:
    struct Row {
        QString brand, raw, automatic, guessed_kind, kind;
    };

    void populate(const NameMappings &current, const PieceHeights &pieces,
                  const MoundSlopes &slopes, const RollOffAngles &rolls,
                  const IngredientGroups &groups);
    QString group_at(int row) const;   ///< empty for "automatic"
    void sync_group(int row);
    double piece_at(int row) const;   ///< -1 for "default"
    void sync_piece(int row);
    void sync_shared(int row, int column);
    void apply_filter();
    void refresh_state();
    void import_csv();
    void export_csv();

    QString name_at(int row) const;
    QString kind_at(int row) const;
    void set_row(int row, const NameMapping &m);
    bool is_mapped(int row) const;

    std::vector<Row> rows_;
    NameMappings stale_;   ///< saved rows for spellings no menu uses any more
    PieceHeights stale_pieces_;
    MoundSlopes stale_slopes_;
    RollOffAngles stale_rolls_;
    IngredientGroups stale_groups_;
    QStringList names_;    ///< every name in use, offered by the "Maps to" boxes

    QComboBox *show_ = nullptr;
    QComboBox *brand_ = nullptr;
    QLineEdit *search_ = nullptr;
    QTableWidget *table_ = nullptr;
    QLabel *count_ = nullptr;
};

}  // namespace bowlfill
