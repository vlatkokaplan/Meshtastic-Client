#include "AppSettings.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QUuid>

AppSettings* AppSettings::s_instance = nullptr;

AppSettings* AppSettings::instance()
{
    if (!s_instance) {
        s_instance = new AppSettings();
    }
    return s_instance;
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    m_connectionName = "app_settings_" + QUuid::createUuid().toString();
}

AppSettings::~AppSettings()
{
    close();
}

bool AppSettings::open()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString dbPath = dataDir + "/settings.db";

    qDebug() << "Opening settings database at:" << dbPath;

    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "Failed to open settings database:" << m_db.lastError().text();
        return false;
    }

    return createTables();
}

void AppSettings::close()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool AppSettings::createTables()
{
    QSqlQuery query(m_db);

    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT,
            updated_at INTEGER
        )
    )")) {
        qWarning() << "Failed to create settings table:" << query.lastError().text();
        return false;
    }

    return true;
}

QVariant AppSettings::value(const QString &key, const QVariant &defaultValue) const
{
    QSqlQuery query(m_db);
    query.prepare("SELECT value FROM settings WHERE key = ?");
    query.addBindValue(key);

    if (query.exec() && query.next()) {
        QString strValue = query.value(0).toString();

        // Try to preserve type from defaultValue
        if (defaultValue.typeId() == QMetaType::Bool) {
            return strValue == "true" || strValue == "1";
        } else if (defaultValue.typeId() == QMetaType::Int) {
            return strValue.toInt();
        } else if (defaultValue.typeId() == QMetaType::Double) {
            return strValue.toDouble();
        }
        return strValue;
    }

    return defaultValue;
}

void AppSettings::setValue(const QString &key, const QVariant &value)
{
    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO settings (key, value, updated_at)
        VALUES (?, ?, strftime('%s', 'now'))
    )");
    query.addBindValue(key);
    query.addBindValue(value.toString());

    if (!query.exec()) {
        qWarning() << "Failed to save setting:" << key << query.lastError().text();
        return;
    }

    emit settingChanged(key, value);
}

// Convenience accessors

QString AppSettings::lastPort() const
{
    return value("connection/last_port", QString()).toString();
}

void AppSettings::setLastPort(const QString &port)
{
    setValue("connection/last_port", port);
}

bool AppSettings::autoConnect() const
{
    return value("connection/auto_connect", false).toBool();
}

void AppSettings::setAutoConnect(bool enabled)
{
    setValue("connection/auto_connect", enabled);
}

int AppSettings::mapZoomLevel() const
{
    return value("map/zoom_level", 10).toInt();
}

void AppSettings::setMapZoomLevel(int level)
{
    setValue("map/zoom_level", level);
}

QString AppSettings::mapTileServer() const
{
    return value("map/tile_server", "https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png").toString();
}

void AppSettings::setMapTileServer(const QString &url)
{
    setValue("map/tile_server", url);
}

bool AppSettings::showOfflineNodes() const
{
    return value("nodes/show_offline", true).toBool();
}

void AppSettings::setShowOfflineNodes(bool show)
{
    setValue("nodes/show_offline", show);
}

int AppSettings::offlineThresholdMinutes() const
{
    return value("nodes/offline_threshold_minutes", 120).toInt();
}

void AppSettings::setOfflineThresholdMinutes(int minutes)
{
    setValue("nodes/offline_threshold_minutes", minutes);
}

bool AppSettings::notificationsEnabled() const
{
    return value("notifications/enabled", true).toBool();
}

void AppSettings::setNotificationsEnabled(bool enabled)
{
    setValue("notifications/enabled", enabled);
}

bool AppSettings::soundEnabled() const
{
    return value("notifications/sound", true).toBool();
}

void AppSettings::setSoundEnabled(bool enabled)
{
    setValue("notifications/sound", enabled);
}

bool AppSettings::hideLocalDevicePackets() const
{
    return value("packets/hide_local_device", false).toBool();
}

void AppSettings::setHideLocalDevicePackets(bool hide)
{
    setValue("packets/hide_local_device", hide);
}

bool AppSettings::mapNodeBlinkEnabled() const
{
    return value("map/node_blink_enabled", true).toBool();
}

void AppSettings::setMapNodeBlinkEnabled(bool enabled)
{
    setValue("map/node_blink_enabled", enabled);
}

int AppSettings::mapNodeBlinkDuration() const
{
    return value("map/node_blink_duration", 10).toInt();
}

void AppSettings::setMapNodeBlinkDuration(int seconds)
{
    setValue("map/node_blink_duration", seconds);
}
