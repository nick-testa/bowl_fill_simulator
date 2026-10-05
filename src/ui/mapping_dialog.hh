#pragma once

#include "core/menu_model.hh"

#include <QDialog>

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
                  const PieceHeights &pieces, QWidget *parent = nullptr);

    /// What to save: only rows that differ from the automatic name or set a kind,
    /// plus any saved rows whose menu spelling no longer appears (kept, not dropped).
    NameMappings mappings() const;
    /// Piece heights by name in use: every row given an explicit height, plus saved
    /// heights for names no row maps to any more.
    PieceHeights pieces() const;

private:
    struct Row {
        QString brand, raw, automatic, guessed_kind, kind;
    };

    void populate(const NameMappings &current, const PieceHeights &pieces);
    double piece_at(int row) const;   ///< -1 for "default"
    void sync_piece(int row);
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
    QStringList names_;    ///< every name in use, offered by the "Maps to" boxes

    QComboBox *show_ = nullptr;
    QComboBox *brand_ = nullptr;
    QLineEdit *search_ = nullptr;
    QTableWidget *table_ = nullptr;
    QLabel *count_ = nullptr;
};

}  // namespace bowlfill
