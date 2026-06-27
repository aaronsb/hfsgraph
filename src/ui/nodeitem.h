// A directory rendered as a rectangular canvas node: a header (name + [+]/[-]
// toggle) over a short file listing. Floats above the canvas with a drop shadow
// (ADR-300 design language). Pure view; carries no structural authority.
#pragma once

#include <QGraphicsItem>
#include <functional>

namespace core {
struct FsNode;
}

namespace ui {

class GraphScene;

class NodeItem : public QGraphicsItem {
  public:
    NodeItem(const core::FsNode *node, bool hasChildren, bool collapsed,
             std::function<void(const core::FsNode *)> onToggle, GraphScene *scene);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    qreal nodeHeight() const { return m_height; }
    static constexpr qreal Width = 210.0;

  protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

  private:
    const core::FsNode *m_node;
    bool m_hasChildren;
    bool m_collapsed;
    std::function<void(const core::FsNode *)> m_onToggle;
    GraphScene *m_scene;
    qreal m_height;
    QRectF m_toggleRect;

    static constexpr int MaxFilesShown = 5;
    static constexpr qreal HeaderH = 28.0;
    static constexpr qreal RowH = 15.0;
};

} // namespace ui
