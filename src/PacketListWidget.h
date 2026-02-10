#ifndef PACKETLISTWIDGET_H
#define PACKETLISTWIDGET_H

#include <QWidget>
#include <QTableView>
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QComboBox>
#include <QList>
#include "MeshtasticProtocol.h"

class NodeManager;

class PacketTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        ColTime,
        ColType,
        ColFrom,
        ColFromAddr,
        ColTo,
        ColToAddr,
        ColChannel,
        ColPortNum,
        ColKey,
        ColContent,
        ColCount
    };

    explicit PacketTableModel(NodeManager *nodeManager, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addPacket(const MeshtasticProtocol::DecodedPacket &packet);
    void clear();

    const MeshtasticProtocol::DecodedPacket &packetAt(int row) const;

private:
    QList<MeshtasticProtocol::DecodedPacket> m_packets;
    NodeManager *m_nodeManager;
    static const int MAX_PACKETS = 10000;

    QString formatNodeName(uint32_t nodeNum) const;
    QString formatNodeId(uint32_t nodeNum) const;
    QString formatContent(const MeshtasticProtocol::DecodedPacket &packet) const;
};

class PacketFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit PacketFilterModel(NodeManager *nodeManager, QObject *parent = nullptr);

    void setTypeFilter(const QString &type);
    void setPortNumFilter(const QString &portNum);
    void setHideLocalDevicePackets(bool hide);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    NodeManager *m_nodeManager;
    QString m_typeFilter;
    QString m_portNumFilter;
    bool m_hideLocalDevicePackets = false;
};

class PacketListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PacketListWidget(NodeManager *nodeManager, QWidget *parent = nullptr);

    void addPacket(const MeshtasticProtocol::DecodedPacket &packet);
    void clear();
    void dumpPacketsToFile(const QString &filePath, int count);

signals:
    void packetSelected(const MeshtasticProtocol::DecodedPacket &packet);

private slots:
    void onRowSelected(const QModelIndex &current, const QModelIndex &previous);
    void onDumpPackets();

private:
    QTableView *m_tableView;
    PacketTableModel *m_model;
    PacketFilterModel *m_filterModel;
    QComboBox *m_typeFilter;
    QComboBox *m_portNumFilter;
    NodeManager *m_nodeManager;

    void setupUI();
};

#endif // PACKETLISTWIDGET_H
