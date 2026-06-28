#include "grouppanel.h"

#include "core/group.h"
#include "graphscene.h"
#include "treemapitem.h"

#include <functional>

#include <QCheckBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace ui {

GroupPanel::GroupPanel(GraphScene *scene, QWidget *parent) : QWidget(parent), m_scene(scene) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(6);

    auto *title = new QLabel(QStringLiteral("Groups"), this);
    QFont tf = title->font();
    tf.setBold(true);
    title->setFont(tf);
    outer->addWidget(title);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *cards = new QWidget(scroll);
    m_cardsLayout = new QVBoxLayout(cards);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(6);
    m_cardsLayout->addStretch(1); // cards are inserted before this
    scroll->setWidget(cards);
    outer->addWidget(scroll, 1);

    outer->addWidget(new QLabel(QStringLiteral("Nesting depth"), this));
    m_legend = new QLabel(this);
    m_legend->setFixedHeight(14);
    m_legend->setScaledContents(true);
    outer->addWidget(m_legend);

    setMinimumWidth(270); // room for the four view-state toggles on one row
    refresh();
}

void GroupPanel::refresh() {
    rebuildCards();

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

void GroupPanel::rebuildCards() {
    // Drop every card but keep the trailing stretch (the last layout item).
    while (m_cardsLayout->count() > 1) {
        QLayoutItem *item = m_cardsLayout->takeAt(0);
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    if (!m_scene)
        return;

    const auto &groups = m_scene->groups().groups();
    if (groups.empty()) {
        auto *empty = new QLabel(QStringLiteral("No groups in this tree."));
        empty->setEnabled(false);
        m_cardsLayout->insertWidget(0, empty);
        return;
    }
    for (const auto &g : groups)
        m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, makeCard(g.get()));
}

QWidget *GroupPanel::makeCard(core::Group *group) {
    auto *card = new QFrame;
    card->setFrameShape(QFrame::StyledPanel);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(6, 4, 6, 4);
    v->setSpacing(4);

    // Header: swatch · name · member count · window-shade toggle.
    auto *header = new QHBoxLayout;
    auto *swatch = new QLabel;
    swatch->setFixedSize(14, 14);
    swatch->setStyleSheet(QStringLiteral("background:%1;border:1px solid #000;")
                              .arg(group->color.name()));
    auto *name = new QLabel(group->name);
    QFont nf = name->font();
    nf.setBold(true);
    name->setFont(nf);
    name->setToolTip(group->kind == core::GroupKind::Rule
                         ? QStringLiteral("git-worktree group (rule-derived)")
                         : QStringLiteral("manual group"));
    auto *count = new QLabel(QStringLiteral("%1").arg(group->members.size()));
    count->setEnabled(false);
    auto *shade = new QToolButton;
    shade->setAutoRaise(true);
    shade->setCheckable(true);
    shade->setChecked(true);
    shade->setText(QStringLiteral("▾")); // ▾
    header->addWidget(swatch);
    header->addWidget(name, 1);
    header->addWidget(count);
    header->addWidget(shade);
    v->addLayout(header);

    // Body: the view-state toggles. Captured `group` stays valid for this card's
    // lifetime — cards are rebuilt (and these lambdas destroyed) on every rescan.
    auto *body = new QWidget;
    auto *b = new QHBoxLayout(body);
    b->setContentsMargins(0, 0, 0, 0);
    b->setSpacing(8);
    auto addToggle = [&](const QString &label, const QString &tip, bool value,
                         std::function<void(bool)> apply) {
        auto *cb = new QCheckBox(label);
        cb->setChecked(value);
        cb->setToolTip(tip);
        connect(cb, &QCheckBox::toggled, this, [this, apply](bool on) {
            apply(on);
            if (m_scene)
                m_scene->updateGroupOverlay();
        });
        b->addWidget(cb);
    };
    addToggle(QStringLiteral("Show"), QStringLiteral("Include this group in the overlay"),
              group->view.visible, [group](bool on) { group->view.visible = on; });
    addToggle(QStringLiteral("Hi"), QStringLiteral("Tint + outline this group's cells"),
              group->view.highlight, [group](bool on) { group->view.highlight = on; });
    addToggle(QStringLiteral("Focus"), QStringLiteral("Dim everything except this group"),
              group->view.focus, [group](bool on) { group->view.focus = on; });
    addToggle(QStringLiteral("Dim"), QStringLiteral("De-emphasise this group's cells"),
              group->view.dim, [group](bool on) { group->view.dim = on; });
    b->addStretch(1);
    v->addWidget(body);

    connect(shade, &QToolButton::toggled, body, [shade, body](bool open) {
        body->setVisible(open);
        shade->setText(open ? QStringLiteral("▾") : QStringLiteral("▸")); // ▾ / ▸
    });

    return card;
}

} // namespace ui
