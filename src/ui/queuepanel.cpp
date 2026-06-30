#include "queuepanel.h"

#include "core/commit.h"
#include "core/move.h"
#include "graphscene.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

namespace ui {

namespace {
// Display label for a destination key (a path today): its basename, or the whole key
// for a root like "/". Source ops cache their own display name on the MoveOp.
QString destLabel(const core::MemberKey &key) {
    const int slash = key.lastIndexOf(QLatin1Char('/'));
    const QString base = slash >= 0 ? key.mid(slash + 1) : key;
    return base.isEmpty() ? key : base;
}
} // namespace

QueuePanel::QueuePanel(GraphScene *scene, QWidget *parent) : QWidget(parent), m_scene(scene) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    m_list = new QListWidget(this);
    m_list->setToolTip(QStringLiteral("Staged moves. Click a row to preview the plan up to that "
                                      "step; later moves stay queued (dimmed)."));
    connect(m_list, &QListWidget::currentRowChanged, this, &QueuePanel::onRowActivated);
    root->addWidget(m_list, 1);

    auto *bar = new QHBoxLayout();
    bar->setSpacing(4);
    m_undo = new QPushButton(QStringLiteral("Undo"), this);
    m_redo = new QPushButton(QStringLiteral("Redo"), this);
    m_clear = new QPushButton(QStringLiteral("Clear"), this);
    m_verify = new QPushButton(QStringLiteral("Verify…"), this);
    m_commit = new QPushButton(QStringLiteral("Commit…"), this);
    m_undo->setToolTip(QStringLiteral("Remove the last staged move (re-doable)"));
    m_redo->setToolTip(QStringLiteral("Restore the last undone move"));
    m_clear->setToolTip(QStringLiteral("Discard the whole staged plan"));
    m_verify->setToolTip(QStringLiteral("Dry-run: prove the plan legal against current disk "
                                        "(ADR-200) — reads only, changes nothing"));
    m_commit->setToolTip(QStringLiteral("Apply the plan to disk — not yet implemented"));
    connect(m_undo, &QPushButton::clicked, this, [this] { m_scene->undoMove(); });
    connect(m_redo, &QPushButton::clicked, this, [this] { m_scene->redoMove(); });
    connect(m_clear, &QPushButton::clicked, this, [this] { m_scene->clearMoves(); });
    connect(m_verify, &QPushButton::clicked, this, [this] { showVerifyReport(); });
    connect(m_commit, &QPushButton::clicked, this, [this] {
        QMessageBox::information(
            this, QStringLiteral("Commit"),
            QStringLiteral("Apply isn't built yet (ADR-200 #16b): it will snapshot, re-verify, "
                           "and apply the moves in order with rollback. Use Verify… to dry-run "
                           "the plan against disk now; until then it stays staged."));
    });
    bar->addWidget(m_undo);
    bar->addWidget(m_redo);
    bar->addWidget(m_clear);
    bar->addStretch(1);
    bar->addWidget(m_verify);
    bar->addWidget(m_commit);
    root->addLayout(bar);

    m_status = new QLabel(this);
    root->addWidget(m_status);

    connect(m_scene, &GraphScene::ledgerChanged, this, &QueuePanel::refresh);
    connect(m_scene, &GraphScene::surfacesChanged, this, &QueuePanel::refresh);
    refresh();
}

void QueuePanel::refresh() {
    const core::Ledger &led = m_scene->ledger();
    const auto &ops = led.ops();
    const int n = static_cast<int>(ops.size());
    const int step = led.step();

    m_populating = true; // setCurrentRow below must not feed back into scrubTo
    m_list->clear();
    // Row 0: the un-staged base (selecting it scrubs every op off).
    m_list->addItem(QStringLiteral("◆ Base — original layout"));
    for (int i = 0; i < n; ++i) {
        const core::MoveOp &op = ops[i];
        auto *item = new QListWidgetItem(
            QStringLiteral("%1.  %2  →  %3").arg(i + 1).arg(op.sourceName, destLabel(op.destParent)));
        if (i + 1 > step) { // staged but past the current preview point — show as pending
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        }
        m_list->addItem(item);
    }
    m_list->setCurrentRow(step); // row index == step (0 = base)
    m_populating = false;

    m_undo->setEnabled(led.canUndo());
    m_redo->setEnabled(led.canRedo());
    m_clear->setEnabled(!led.empty());
    m_verify->setEnabled(!led.empty());
    m_commit->setEnabled(!led.empty());
    if (n == 0)
        m_status->setText(QStringLiteral("No moves staged"));
    else
        m_status->setText(QStringLiteral("Previewing %1 of %2 staged move%3")
                              .arg(step).arg(n).arg(n == 1 ? QString() : QStringLiteral("s")));
}

void QueuePanel::onRowActivated(int row) {
    if (m_populating || row < 0)
        return;
    m_scene->scrubTo(row); // row index is the step (0 = base, i = after op i)
}

void QueuePanel::showVerifyReport() {
    const core::CommitPlan plan = m_scene->verifyLedger();
    if (plan.ops.empty()) {
        QMessageBox::information(this, QStringLiteral("Verify"),
                                 QStringLiteral("No moves staged to verify."));
        return;
    }
    // One line per op (numbered to match the queue rows). Blocked ops carry their reason.
    QStringList lines;
    for (int i = 0; i < static_cast<int>(plan.ops.size()); ++i) {
        const core::OpVerification &v = plan.ops[i];
        const QString mark = v.status == core::VerifyStatus::Ok ? QStringLiteral("✓")
                                                                : QStringLiteral("✗");
        lines << QStringLiteral("%1 %2. %3").arg(mark).arg(i + 1).arg(v.detail);
    }
    const QString summary =
        plan.allClear()
            ? QStringLiteral("All %1 staged move(s) verified — the plan is legal against the "
                             "current disk.")
                  .arg(plan.okCount())
            : QStringLiteral("%1 of %2 move(s) OK, %3 blocked. Apply will refuse the blocked "
                             "ones until you re-scan or adjust the plan.")
                  .arg(plan.okCount())
                  .arg(plan.ops.size())
                  .arg(plan.blockedCount());

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Verify plan"));
    box.setIcon(plan.allClear() ? QMessageBox::Information : QMessageBox::Warning);
    box.setText(summary);
    box.setDetailedText(lines.join(QLatin1Char('\n')));
    box.exec();
}

} // namespace ui
