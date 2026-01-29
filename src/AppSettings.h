#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariant>

class AppSettings : public QObject
{
    Q_OBJECT

public:
    static AppSettings *instance();

    bool open();
    void close();

    // Generic get/set for any setting
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    void setValue(const QString &key, const QVariant &value);

    // Convenience accessors for common settings
    QString lastPort() const;
    void setLastPort(const QString &port);

    bool autoConnect() const;
    void setAutoConnect(bool enabled);

    int mapZoomLevel() const;
    void setMapZoomLevel(int level);

    QString mapTileServer() const;
    void setMapTileServer(const QString &url);

    bool showOfflineNodes() const;
    void setShowOfflineNodes(bool show);

    int offlineThresholdMinutes() const;
    void setOfflineThresholdMinutes(int minutes);

    bool notificationsEnabled() const;
    void setNotificationsEnabled(bool enabled);

    bool soundEnabled() const;
    void setSoundEnabled(bool enabled);

    bool hideLocalDevicePackets() const;
    void setHideLocalDevicePackets(bool hide);

    bool mapNodeBlinkEnabled() const;
    void setMapNodeBlinkEnabled(bool enabled);

    int mapNodeBlinkDuration() const;
    void setMapNodeBlinkDuration(int seconds);

    bool darkTheme() const;
    void setDarkTheme(bool dark);

    int messageFontSize() const;
    void setMessageFontSize(int size);

signals:
    void settingChanged(const QString &key, const QVariant &value);

private:
    explicit AppSettings(QObject *parent = nullptr);
    ~AppSettings();

    static AppSettings *s_instance;
    QSqlDatabase m_db;
    QString m_connectionName;

    bool createTables();
};

#endif // APPSETTINGS_H
