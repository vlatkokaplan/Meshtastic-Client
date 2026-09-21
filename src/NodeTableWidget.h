#ifndef NODETABLEWIDGET_H
#define NODETABLEWIDGET_H

#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QTableWidget>
#include <QWidget>

#include "NodeManager.h"

// The searchable node list: header, filter box and table.
//
// Split out of MainWindow, which had grown past three thousand lines with the
// table's construction, its 280-line repopulate and its context menu all mixed
// in with packet dispatch and database lifecycle.
//
// It owns presentation only. Every action a user picks from the context menu
// leaves as a signal, because acting on it needs the radio, the map or the
// database - none of which belong here.
class NodeTableWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NodeTableWidget(NodeManager *nodes, QWidget *parent = nullptr);

    // Rebuilds from NodeManager, preserving selection and scroll position.
    void refresh();

    // Marks the cached ordering stale; the next refresh() re-sorts.
    void markSortStale() { m_sortNeeded = true; }

    // Selects and scrolls to a node. Returns false if it is not currently
    // listed, which happens when a filter hides it.
    bool selectNode(uint32_t nodeNum);

    uint32_t selectedNode() const;

signals:
    void nodeActivated(uint32_t nodeNum);          // single click
    void directMessageRequested(uint32_t nodeNum);
    void tracerouteRequested(uint32_t nodeNum);
    void nodeInfoRequested(uint32_t nodeNum);
    void telemetryRequested(uint32_t nodeNum);
    void positionRequested(uint32_t nodeNum);
    void centerOnMapRequested(uint32_t nodeNum);
    void trackRequested(uint32_t nodeNum);
    void clearTrackRequested();

private slots:
    void onItemClicked(QTableWidgetItem *item);
    void onContextMenu(const QPoint &pos);

private:
    void setupUI();
    uint32_t nodeNumForRow(int row) const;

    NodeManager *m_nodes = nullptr;

    QLabel *m_heading = nullptr;
    QLineEdit *m_search = nullptr;
    QTableWidget *m_table = nullptr;

    QList<NodeInfo> m_sorted;
    bool m_sortNeeded = true;
};

#endif // NODETABLEWIDGET_H
