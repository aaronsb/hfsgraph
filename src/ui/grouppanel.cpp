#include "grouppanel.h"

#include "core/fsnode.h"
#include "core/group.h"
#include "frameitem.h"
#include "graphscene.h"
#include "treemapitem.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

namespace ui {

namespace {
// Column layout of the group table. The first two are read-only; the last four are
// checkable view-state toggles (order matches the bulk buttons + onItemChanged).
enum Col { ColName = 0, ColMembers, ColShow, ColHi, ColFocus, ColDim, NumCols };
constexpr bool isStateCol(int c) {
    return c >= ColShow;
}
} // namespace

GroupPanel::GroupPanel(GraphScene *scene, QWidget *parent) : QWidget(parent), m_scene(scene) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(6);

    auto boldLabel = [this](const QString &text) {
        auto *l = new QLabel(text, this);
        QFont f = l->font();
        f.setBold(true);
        l->setFont(f);
        return l;
    };

    // Bases section (ADR-304): the open level-0 surfaces, each removable.
    outer->addWidget(boldLabel(QStringLiteral("Bases")));
    m_basesLayout = new QVBoxLayout;
    m_basesLayout->setContentsMargins(0, 0, 0, 0);
    m_basesLayout->setSpacing(2);
    outer->addLayout(m_basesLayout);

    outer->addWidget(boldLabel(QStringLiteral("Groups")));

    // Bulk-action bars. A row of small buttons that drive row selection, and a row
    // that applies a view state to every selected group (or all, if none selected).
    auto mkBtn = [this](const QString &text, const QString &tip, std::function<void()> onClick) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, std::move(onClick));
        return b;
    };

    auto *selRow = new QHBoxLayout;
    selRow->setContentsMargins(0, 0, 0, 0);
    selRow->setSpacing(2);
    selRow->addWidget(new QLabel(QStringLiteral("Select:"), this));
    selRow->addWidget(mkBtn(QStringLiteral("All"), QStringLiteral("Select every group"),
                            [this] { selectAllRows(true); }));
    selRow->addWidget(mkBtn(QStringLiteral("None"), QStringLiteral("Clear the selection"),
                            [this] { selectAllRows(false); }));
    selRow->addWidget(mkBtn(QStringLiteral("Invert"), QStringLiteral("Invert the selection"),
                            [this] { invertSelection(); }));
    selRow->addStretch(1);
    outer->addLayout(selRow);

    auto *appRow = new QHBoxLayout;
    appRow->setContentsMargins(0, 0, 0, 0);
    appRow->setSpacing(2);
    const QString to = QStringLiteral(" the selected groups (or all if none selected)");
    appRow->addWidget(new QLabel(QStringLiteral("Set:"), this));
    appRow->addWidget(mkBtn(QStringLiteral("Hi"), QStringLiteral("Highlight") + to, [this] {
        applyToTargets([](core::Group &g) { g.view.highlight = true; });
    }));
    appRow->addWidget(mkBtn(QStringLiteral("No Hi"), QStringLiteral("Un-highlight") + to, [this] {
        applyToTargets([](core::Group &g) { g.view.highlight = false; });
    }));
    appRow->addWidget(mkBtn(QStringLiteral("Show"), QStringLiteral("Show") + to, [this] {
        applyToTargets([](core::Group &g) { g.view.visible = true; });
    }));
    appRow->addWidget(mkBtn(QStringLiteral("Hide"), QStringLiteral("Hide") + to, [this] {
        applyToTargets([](core::Group &g) { g.view.visible = false; });
    }));
    appRow->addWidget(
        mkBtn(QStringLiteral("Clear"), QStringLiteral("Clear Hi / Focus / Dim on") + to, [this] {
            applyToTargets(
                [](core::Group &g) { g.view.highlight = g.view.focus = g.view.dim = false; });
        }));
    appRow->addStretch(1);
    outer->addLayout(appRow);

    // The group table. Rows are the selection unit for bulk ops; the four state
    // columns are per-group checkboxes.
    m_table = new QTableWidget(0, NumCols, this);
    // Compact single-letter headers for the state columns (with tooltips) so the
    // Group name column keeps the width — repo names can be long.
    m_table->setHorizontalHeaderLabels({QStringLiteral("Group"), QStringLiteral("N"),
                                        QStringLiteral("S"), QStringLiteral("H"),
                                        QStringLiteral("F"), QStringLiteral("D")});
    const std::pair<int, QString> hints[] = {
        {ColMembers, QStringLiteral("Member count")},
        {ColShow, QStringLiteral("Show in the overlay")},
        {ColHi, QStringLiteral("Highlight (tint + outline)")},
        {ColFocus, QStringLiteral("Focus (dim everything else)")},
        {ColDim, QStringLiteral("Dim this group")}};
    for (const auto &[col, tip] : hints)
        m_table->horizontalHeaderItem(col)->setToolTip(tip);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setIconSize(QSize(12, 12));
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Name takes the remaining width; the count + state columns are fixed and narrow
    // so a long group name isn't squeezed to an ellipsis.
    auto *hh = m_table->horizontalHeader();
    hh->setMinimumSectionSize(22);
    hh->setSectionResizeMode(ColName, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColMembers, QHeaderView::Fixed);
    m_table->setColumnWidth(ColMembers, 32);
    for (int c = ColShow; c < NumCols; ++c) {
        hh->setSectionResizeMode(c, QHeaderView::Fixed);
        m_table->setColumnWidth(c, 28);
    }
    connect(m_table, &QTableWidget::itemChanged, this, &GroupPanel::onItemChanged);
    outer->addWidget(m_table, 1);

    outer->addWidget(new QLabel(QStringLiteral("Nesting depth"), this));
    m_legend = new QLabel(this);
    m_legend->setFixedHeight(14);
    m_legend->setScaledContents(true);
    outer->addWidget(m_legend);

    setMinimumWidth(300);
    if (m_scene)
        connect(m_scene, &GraphScene::surfacesChanged, this, &GroupPanel::refresh);
    refresh();
}

void GroupPanel::refresh() {
    rebuildBases();
    rebuildTable();

    // Depth-ramp legend: the same colours the treemap paints per nesting depth.
    constexpr int depths = 7, band = 24, h = 14;
    QPixmap pm(depths * band, h);
    QPainter p(&pm);
    const auto ramp = static_cast<TreemapItem::Ramp>(m_scene ? m_scene->colorRamp() : 0);
    for (int d = 0; d < depths; ++d)
        p.fillRect(QRect(d * band, 0, band, h), TreemapItem::depthColor(ramp, d));
    p.end();
    m_legend->setPixmap(pm);
}

void GroupPanel::rebuildBases() {
    // Clear the existing base rows (this layout has no trailing stretch).
    while (QLayoutItem *item = m_basesLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    if (!m_scene)
        return;

    const std::vector<FrameItem *> bases = m_scene->baseFrames();
    if (bases.empty()) {
        auto *empty = new QLabel(QStringLiteral("No bases — use “Add base folder”."));
        empty->setEnabled(false);
        empty->setWordWrap(true);
        m_basesLayout->addWidget(empty);
        return;
    }
    for (FrameItem *base : bases) {
        auto *row = new QWidget;
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        const QString path = base->node() ? base->node()->path : QString();
        const QString name = base->node() ? base->node()->name : QStringLiteral("(base)");
        auto *label = new QLabel(name);
        label->setToolTip(path);
        auto *remove = new QToolButton;
        remove->setAutoRaise(true);
        remove->setText(QStringLiteral("×"));
        remove->setToolTip(QStringLiteral("Remove this base surface"));
        // The row is rebuilt on surfacesChanged after removal, so capturing `base`
        // raw is safe for the row's lifetime.
        connect(remove, &QToolButton::clicked, this, [this, base] {
            if (m_scene)
                m_scene->removeBase(base);
        });
        h->addWidget(label, 1);
        h->addWidget(remove);
        m_basesLayout->addWidget(row);
    }
}

void GroupPanel::rebuildTable() {
    m_populating = true; // setItem / setCheckState below must not fire onItemChanged
    m_table->clearContents();
    m_table->setRowCount(0);

    if (m_scene) {
        const auto &groups = m_scene->groups().groups();
        m_table->setRowCount(static_cast<int>(groups.size()));
        int row = 0;
        for (const auto &gp : groups) {
            const core::Group *g = gp.get();

            auto *name = new QTableWidgetItem(g->name);
            name->setData(Qt::UserRole, g->id); // row → group identity
            QPixmap swatch(12, 12);
            swatch.fill(g->color);
            name->setIcon(QIcon(swatch));
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            name->setToolTip(g->kind == core::GroupKind::Rule
                                 ? QStringLiteral("git-worktree group (rule-derived)")
                                 : QStringLiteral("manual group"));
            m_table->setItem(row, ColName, name);

            auto *count = new QTableWidgetItem(QString::number(g->members.size()));
            count->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            count->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(row, ColMembers, count);

            auto stateItem = [](bool on) {
                auto *it = new QTableWidgetItem;
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
                it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
                return it;
            };
            m_table->setItem(row, ColShow, stateItem(g->view.visible));
            m_table->setItem(row, ColHi, stateItem(g->view.highlight));
            m_table->setItem(row, ColFocus, stateItem(g->view.focus));
            m_table->setItem(row, ColDim, stateItem(g->view.dim));
            ++row;
        }
    }
    m_populating = false;
}

core::Group *GroupPanel::groupForRow(int row) const {
    if (!m_scene)
        return nullptr;
    const QTableWidgetItem *name = m_table->item(row, ColName);
    return name ? m_scene->groups().find(name->data(Qt::UserRole).toString()) : nullptr;
}

void GroupPanel::onItemChanged(QTableWidgetItem *item) {
    if (m_populating || !item || !isStateCol(item->column()))
        return;
    core::Group *g = groupForRow(item->row());
    if (!g)
        return;
    const bool on = item->checkState() == Qt::Checked;
    switch (item->column()) {
    case ColShow:
        g->view.visible = on;
        break;
    case ColHi:
        g->view.highlight = on;
        break;
    case ColFocus:
        g->view.focus = on;
        break;
    case ColDim:
        g->view.dim = on;
        break;
    }
    if (m_scene)
        m_scene->updateGroupOverlay();
}

void GroupPanel::applyToTargets(const std::function<void(core::Group &)> &fn) {
    if (!m_scene)
        return;
    QSet<int> selected;
    for (const QModelIndex &idx : m_table->selectionModel()->selectedRows())
        selected.insert(idx.row());
    const bool all = selected.isEmpty(); // no selection → act on every group

    // One pass: resolve each row's group once (avoids a second lookup), apply the
    // change to targets, and re-sync every row's checkboxes from the (possibly
    // mutated) group. Guarded so the setCheckState calls don't re-fire onItemChanged.
    m_populating = true;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        core::Group *g = groupForRow(r);
        if (!g)
            continue;
        if (all || selected.contains(r))
            fn(*g);
        m_table->item(r, ColShow)->setCheckState(g->view.visible ? Qt::Checked : Qt::Unchecked);
        m_table->item(r, ColHi)->setCheckState(g->view.highlight ? Qt::Checked : Qt::Unchecked);
        m_table->item(r, ColFocus)->setCheckState(g->view.focus ? Qt::Checked : Qt::Unchecked);
        m_table->item(r, ColDim)->setCheckState(g->view.dim ? Qt::Checked : Qt::Unchecked);
    }
    m_populating = false;
    m_scene->updateGroupOverlay();
}

void GroupPanel::selectAllRows(bool on) {
    if (on)
        m_table->selectAll();
    else
        m_table->clearSelection();
}

void GroupPanel::invertSelection() {
    QSet<int> selected;
    for (const QModelIndex &idx : m_table->selectionModel()->selectedRows())
        selected.insert(idx.row());
    QItemSelection inverted;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (selected.contains(r))
            continue;
        inverted.select(m_table->model()->index(r, 0), m_table->model()->index(r, NumCols - 1));
    }
    m_table->selectionModel()->select(inverted, QItemSelectionModel::ClearAndSelect);
}

} // namespace ui
