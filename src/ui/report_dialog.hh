#pragma once

#include "core/feasibility.hh"

#include <QDialog>

class QLabel;
class QTableWidget;

namespace bowlfill {

///
/// The culinary team's question in one place: for a given brand, which recipes can
/// actually be made as written? Each row is one recipe under both dispense models,
/// worst first.
///
class ReportDialog : public QDialog {
    Q_OBJECT

public:
    ReportDialog(const BrandAudit &audit, const QString &assumptions, QWidget *parent = nullptr);

signals:
    /// A recipe the user wants to open in the simulator.
    void recipe_chosen(const QString &recipe);
    /// The combinatorial workup over this brand, which the main window owns.
    void full_report_requested();

private:
    void populate();
    void show_detail(int row);

    BrandAudit audit_;
    QTableWidget *table_ = nullptr;
    QLabel *detail_ = nullptr;
};

}  // namespace bowlfill
