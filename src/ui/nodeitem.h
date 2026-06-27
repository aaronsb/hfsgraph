// A directory as a rounded-rect canvas card. A "window-shade" toggle rolls the
// card between two states:
//   * shaded  -> a compact node showing stats (files, dirs, size on disk);
//   * open    -> a resizable file viewer that toggles between an icon grid and a
//                detail list (name/size/type/date), backed by a QFileSystemModel.
// The file viewer is built lazily (only when first opened). A separate [+]/[-]
// toggle collapses/expands child directory nodes in the graph. Floats above the
// canvas with a drop shadow (ADR-300). Pure view; no structural authority.
#pragma once

#include <QGraphicsItem>
#include <QRectF>
#include <QString>
#include <functional>

class QGraphicsProxyWidget;
class QFileSystemModel;
class QStackedWidget;

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

    qreal nodeWidth() const { return m_width; }
    qreal nodeHeight() const { return m_height; }

  protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

  private:
    QRectF slotRect(int fromRight) const;
    QRectF collapseToggleRect() const;
    QRectF shadeToggleRect() const;
    QRectF viewToggleRect() const;
    QRectF resizeHandleRect() const;
    qreal titleRight() const;
    void buildViewer();
    void toggleShade();
    void recomputeHeight();
    void updateListGeometry();
    static QString humanSize(qint64 bytes);

    const core::FsNode *m_node;
    bool m_hasChildren;
    bool m_collapsed;
    bool m_hasFiles;
    bool m_shaded = true; // start rolled up (compact stats); also keeps the graph light
    int m_viewMode = 0;   // 0 = icon grid, 1 = detail list
    std::function<void(const core::FsNode *)> m_onToggle;
    GraphScene *m_scene;
    qreal m_width;
    qreal m_height;
    qreal m_openListH; // viewer height when open (resizable)
    bool m_resizing = false;
    QString m_stats1; // "N files · M dirs"
    QString m_stats2; // "1.2 MB on disk"
    QGraphicsProxyWidget *m_proxy = nullptr;
    QFileSystemModel *m_fsModel = nullptr;
    QStackedWidget *m_stack = nullptr;

    static constexpr qreal HeaderH = 30.0;
    static constexpr qreal LineH = 18.0;
    static constexpr qreal DefaultWidth = 270.0;
    static constexpr qreal DefaultListH = 156.0;
    static constexpr qreal MinWidth = 170.0;
    static constexpr qreal MinListH = 60.0;
    static constexpr qreal SlotStep = 22.0;
};

} // namespace ui
