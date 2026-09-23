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
        // Serialized Config.LoRaConfig as last received from the device. A
        // set_config replaces the whole section, so saves start from this and
        // only overwrite the fields the UI edits.
        QByteArray raw;
    };

    // Device config
    struct DeviceSettings {
        int role = 0;  // 0=Client, 1=ClientMute, 2=Router, 3=RouterClient, etc.
        int buttonGpio = 0;
        int buzzerGpio = 0;
        int rebroadcastMode = 0;
        int nodeInfoBroadcastSecs = 900;
        bool doubleTapAsButtonPress = false;
        bool disableTripleClick = false;
        QString tzdef;
        bool ledHeartbeatDisabled = false;
        QByteArray raw;  // serialized Config.DeviceConfig, see LoRaConfig::raw
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
        QByteArray raw;  // serialized Config.PositionConfig, see LoRaConfig::raw
    };

    // Security config. Also holds the node's keys, which the UI never
    // touches: they only travel inside `raw`, so saves must start from it.
    struct SecuritySettings {
        bool serialEnabled = true;       // Stream API over serial
        bool debugLogApiEnabled = false; // debug log lines to API clients
        QByteArray raw;
    };

    // Channel config (up to 8 channels)
    struct ChannelConfig {
        int index = 0;
        int role = 0;   // 0=Disabled, 1=Primary, 2=Secondary
        QString name;
        QByteArray psk;
        bool uplinkEnabled = false;
        bool downlinkEnabled = false;
        // Not edited in the UI, but a set_channel replaces the whole channel,
        // so these must be sent back as received. positionPrecision 0 turns
        // position sharing off on the channel.
        uint32_t id = 0;
        uint32_t positionPrecision = 0;
        bool isMuted = false;
    };

    explicit DeviceConfig(QObject *parent = nullptr);

    // Accessors
    LoRaConfig loraConfig() const { return m_lora; }
    DeviceSettings deviceConfig() const { return m_device; }
    PositionSettings positionConfig() const { return m_position; }
    SecuritySettings securityConfig() const { return m_security; }
    // True when the UI changed security settings since the device sent them
    bool securityEdited() const;
    QList<ChannelConfig> channels() const { return m_channels; }
    ChannelConfig channel(int index) const;

    // Setters (emit configChanged)
    void setLoRaConfig(const LoRaConfig &config);
    void setDeviceConfig(const DeviceSettings &config);
    void setPositionConfig(const PositionSettings &config);
    void setSecurityConfig(const SecuritySettings &config);
    void setChannel(int index, const ChannelConfig &config);

    // Update from received packet fields
    void updateFromLoRaPacket(const QVariantMap &fields);
    void updateFromDevicePacket(const QVariantMap &fields);
    void updateFromPositionPacket(const QVariantMap &fields);
    void updateFromSecurityPacket(const QVariantMap &fields);
    void updateFromChannelPacket(const QVariantMap &fields);

    // Check if config has been received
    bool hasLoRaConfig() const { return m_hasLora; }
    bool hasDeviceConfig() const { return m_hasDevice; }
    bool hasPositionConfig() const { return m_hasPosition; }
    bool hasSecurityConfig() const { return m_hasSecurity; }

    // Enum choices, read from the protobuf descriptors so they always match
    // the vendored upstream protos. `value` is the protobuf enum number, which
    // is not the same as a list position once values are skipped.
    struct EnumOption {
        int value = 0;
        QString name;       // enum identifier, e.g. "EU_868"
        QString label;      // upstream display label, or the name if none
        bool deprecated = false;
    };
    static QList<EnumOption> regionOptions();
    static QList<EnumOption> modemPresetOptions();
    static QList<EnumOption> deviceRoleOptions();
    static QList<EnumOption> gpsModeOptions();

    // Bandwidth as stored in LoRaConfig.bandwidth: kHz rounded, except the
    // firmware's codes for fractional widths (8 = 7.8 kHz, 31 = 31.25, ...).
    // Matches bwCodeToKHz in the firmware's RadioInterface.
    struct BandwidthOption {
        int code;
        QString label;
        bool wideLoraOnly;  // 2.4 GHz (LORA_24) only
    };
    static QList<BandwidthOption> bandwidthOptions();

    // The bandwidth code, spreading factor and coding rate (4/cr) a preset
    // uses, as modemPresetToParams in the firmware. wideLora for LORA_24.
    static void presetModemParams(int preset, bool wideLora, int &bwCode, int &sf, int &cr);
    static constexpr int REGION_LORA_24 = 13;

    static QString regionName(int value);       // "EU_868"
    static QString modemPresetName(int value);  // "Long Range - Fast"
    static QString deviceRoleName(int value);   // "Client"

signals:
    void loraConfigChanged();
    void deviceConfigChanged();
    void positionConfigChanged();
    void securityConfigChanged();
    void channelConfigChanged(int index);

private:
    LoRaConfig m_lora;
    DeviceSettings m_device;
    PositionSettings m_position;
    SecuritySettings m_security;
    SecuritySettings m_securityFromDevice;  // as last received, for securityEdited()
    QList<ChannelConfig> m_channels;

    bool m_hasLora = false;
    bool m_hasDevice = false;
    bool m_hasPosition = false;
    bool m_hasSecurity = false;
};

#endif // DEVICECONFIG_H
