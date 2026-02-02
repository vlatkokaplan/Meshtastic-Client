#ifndef TRACEROUTEWIDGET_H
#define TRACEROUTEWIDGET_H

#include <QWidget>
#include <QTableView>
#include <QAbstractTableModel>
#include <QList>
#include "MeshtasticProtocol.h"

class NodeManager;
class Database;

class TracerouteTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        ColTime,
        ColFrom,
        ColTo,
        ColRouteTo,
        ColRouteBack,
        ColSnrTo,
        ColSnrBack,
        ColCount
    };

    explicit TracerouteTableModel(NodeManager *nodeManager, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addTraceroute(const MeshtasticProtocol::DecodedPacket &packet);
    void addTracerouteFromDb(uint32_t from, uint32_t to, quint64 timestamp,
                             const QStringList &routeTo, const QStringList &routeBack,
                             const QStringList &snrTo, const QStringList &snrBack);
    void clear();

    // Access to stored traceroute data
    struct TracerouteData {
        uint32_t from;
        uint32_t to;
        QStringList routeTo;
        QStringList routeBack;
        QStringList snrTo;
        QStringList snrBack;
    };
    TracerouteData getTraceroute(int row) const;
    int tracerouteCount() const { return m_traceroutes.size(); }

private:
    struct Traceroute
    {
        quint64 timestamp;
        uint32_t from;
        uint32_t to;
        QStringList routeTo;
        QStringList routeBack;
        QStringList snrTo;
        QStringList snrBack;
    };

    QList<Traceroute> m_traceroutes;
    NodeManager *m_nodeManager;
    static const int MAX_TRACEROUTES = 1000;

    QString formatNodeName(uint32_t nodeNum) const;
};

class TracerouteWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TracerouteWidget(NodeManager *nodeManager, Database *database, QWidget *parent = nullptr);

    void addTraceroute(const MeshtasticProtocol::DecodedPacket &packet);
    void loadFromDatabase();
    void clear();

    // Get route data for selected traceroute (for map visualization)
    struct RouteNode {
        uint32_t nodeNum;
        QString name;
        float snr;
    };
    QList<RouteNode> getSelectedRoute() const;

signals:
    void tracerouteSelected(uint32_t fromNode, uint32_t toNode);

private slots:
    void onSelectionChanged();

private:
    QTableView *m_tableView;
    TracerouteTableModel *m_model;
    NodeManager *m_nodeManager;
    Database *m_database;

    void setupUI();
};

#endif // TRACEROUTEWIDGET_H
