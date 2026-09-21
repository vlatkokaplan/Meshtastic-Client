#ifndef ANALYTICSWIDGET_H
#define ANALYTICSWIDGET_H

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

#include "MeshAnalytics.h"

class Database;
class DeviceConfig;
class NodeManager;

// Draws a horizontal proportion bar with an optional limit marker. Used for
// decode success and for airtime against a duty cycle ceiling.
class ProportionBar : public QWidget
{
    Q_OBJECT
public:
    explicit ProportionBar(QWidget *parent = nullptr);

    // value/max as a filled fraction; limit < 0 hides the marker
    void setValue(double value, double max, double limit = -1.0);
    void setFillColor(const QColor &c);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double m_value = 0.0;
    double m_max = 1.0;
    double m_limit = -1.0;
    QColor m_fill;
};

// The analyst view: what the mesh is doing, derived entirely from data already
// received. Nothing in this tab transmits.
class AnalyticsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AnalyticsWidget(NodeManager *nodes, DeviceConfig *config, QWidget *parent = nullptr);

    void setDatabase(Database *db);

public slots:
    void refresh();

private slots:
    void onWindowChanged(int index);

private:
    void setupUI();
    void updateDecode(const QDateTime &since, MeshAnalytics &analytics);
    void updateAirtime(const QDateTime &since, MeshAnalytics &analytics);
    void updateReachability(const QDateTime &since, MeshAnalytics &analytics);
    void updateChurn(const QDateTime &since, MeshAnalytics &analytics);

    QDateTime windowStart() const;
    double dutyCycleLimitPercent() const;

    NodeManager *m_nodes = nullptr;
    DeviceConfig *m_config = nullptr;
    Database *m_db = nullptr;

    QComboBox *m_windowCombo = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QLabel *m_updatedLabel = nullptr;
    QTimer *m_autoRefresh = nullptr;

    // Decode success
    QLabel *m_decodeHeadline = nullptr;
    ProportionBar *m_decodeBar = nullptr;
    QLabel *m_decodeByPort = nullptr;
    QLabel *m_decodeByChannel = nullptr;

    // Airtime
    QLabel *m_airtimeHeadline = nullptr;
    ProportionBar *m_airtimeBar = nullptr;
    QTableWidget *m_airtimeTable = nullptr;

    // Reachability
    QLabel *m_reachHeadline = nullptr;
    QLabel *m_reachDetail = nullptr;
    QTableWidget *m_articulationTable = nullptr;

    // Route churn
    QTableWidget *m_churnTable = nullptr;
};

#endif // ANALYTICSWIDGET_H
