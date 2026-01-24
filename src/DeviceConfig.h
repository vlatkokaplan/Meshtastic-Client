#ifndef DEVICECONFIG_H
#define DEVICECONFIG_H

#include <QObject>
#include <QString>
#include <QVariantMap>

// Holds all device configuration state
class DeviceConfig : public QObject
{
    Q_OBJECT

public:
    // LoRa/Radio config
    struct LoRaConfig {
        bool usePreset = true;
        int modemPreset = 0;  // 0=LongFast, 1=LongSlow, 2=VLongSlow, 3=MedSlow, 4=MedFast, 5=ShortSlow, 6=ShortFast
        int region = 0;       // 0=Unset, 1=US, 2=EU433, 3=EU868, etc.
        int hopLimit = 3;
        bool txEnabled = true;
        int txPower = 0;      // 0 = use default
        int channelNum = 0;
        bool overrideDutyCycle = false;
        float frequencyOffset = 0.0f;
        // Advanced manual settings
        int bandwidth = 0;
        int spreadFactor = 0;
        int codingRate = 0;
    };

    // Device config
    struct DeviceSettings {
        int role = 0;  // 0=Client, 1=ClientMute, 2=Router, 3=RouterClient, etc.
        bool serialEnabled = true;
        bool debugLogEnabled = false;
        int buttonGpio = 0;
        int buzzerGpio = 0;
        int rebroadcastMode = 0;
        int nodeInfoBroadcastSecs = 900;
        bool doubleTapAsButtonPress = false;
        bool isManaged = false;
        bool disableTripleClick = false;
        QString tzdef;
        bool ledHeartbeatDisabled = false;
    };

    // Position config
    struct PositionSettings {
        int positionBroadcastSecs = 900;
        bool smartPositionEnabled = true;
        bool fixedPosition = false;
        bool gpsEnabled = true;
        int gpsUpdateInterval = 120;
        int gpsAttemptTime = 120;
        int positionFlags = 0;
        int broadcastSmartMinDistance = 100;
        int broadcastSmartMinIntervalSecs = 30;
        int gpsMode = 0;  // 0=Disabled, 1=Enabled, 2=NotPresent
    };

    // Channel config (up to 8 channels)
    struct ChannelConfig {
        int index = 0;
        int role = 0;   // 0=Disabled, 1=Primary, 2=Secondary
        QString name;
        QByteArray psk;
        bool uplinkEnabled = false;
        bool downlinkEnabled = false;
    };

    explicit DeviceConfig(QObject *parent = nullptr);

    // Accessors
    LoRaConfig loraConfig() const { return m_lora; }
    DeviceSettings deviceConfig() const { return m_device; }
    PositionSettings positionConfig() const { return m_position; }
    QList<ChannelConfig> channels() const { return m_channels; }
    ChannelConfig channel(int index) const;

    // Setters (emit configChanged)
    void setLoRaConfig(const LoRaConfig &config);
    void setDeviceConfig(const DeviceSettings &config);
    void setPositionConfig(const PositionSettings &config);
    void setChannel(int index, const ChannelConfig &config);

    // Update from received packet fields
    void updateFromLoRaPacket(const QVariantMap &fields);
    void updateFromDevicePacket(const QVariantMap &fields);
    void updateFromPositionPacket(const QVariantMap &fields);
    void updateFromChannelPacket(const QVariantMap &fields);

    // Check if config has been received
    bool hasLoRaConfig() const { return m_hasLora; }
    bool hasDeviceConfig() const { return m_hasDevice; }
    bool hasPositionConfig() const { return m_hasPosition; }

    // Region names
    static QStringList regionNames();
    static QStringList modemPresetNames();
    static QStringList deviceRoleNames();
    static QStringList gpsModeNames();

signals:
    void loraConfigChanged();
    void deviceConfigChanged();
    void positionConfigChanged();
    void channelConfigChanged(int index);

private:
    LoRaConfig m_lora;
    DeviceSettings m_device;
    PositionSettings m_position;
    QList<ChannelConfig> m_channels;

    bool m_hasLora = false;
    bool m_hasDevice = false;
    bool m_hasPosition = false;
};

#endif // DEVICECONFIG_H
