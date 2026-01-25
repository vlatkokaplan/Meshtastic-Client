#include "DeviceConfig.h"
#include <QDebug>

DeviceConfig::DeviceConfig(QObject *parent)
    : QObject(parent)
{
    // Initialize 8 channels
    for (int i = 0; i < 8; i++) {
        ChannelConfig ch;
        ch.index = i;
        ch.role = (i == 0) ? 1 : 0;  // First channel is primary by default
        m_channels.append(ch);
    }
}

DeviceConfig::ChannelConfig DeviceConfig::channel(int index) const
{
    if (index >= 0 && index < m_channels.size()) {
        return m_channels[index];
    }
    return ChannelConfig();
}

void DeviceConfig::setLoRaConfig(const LoRaConfig &config)
{
    m_lora = config;
    m_hasLora = true;
    emit loraConfigChanged();
}

void DeviceConfig::setDeviceConfig(const DeviceSettings &config)
{
    m_device = config;
    m_hasDevice = true;
    emit deviceConfigChanged();
}

void DeviceConfig::setPositionConfig(const PositionSettings &config)
{
    m_position = config;
    m_hasPosition = true;
    emit positionConfigChanged();
}

void DeviceConfig::setChannel(int index, const ChannelConfig &config)
{
    if (index >= 0 && index < m_channels.size()) {
        m_channels[index] = config;
        emit channelConfigChanged(index);
    }
}

void DeviceConfig::updateFromLoRaPacket(const QVariantMap &fields)
{
    qDebug() << "DeviceConfig::updateFromLoRaPacket called with fields:" << fields.keys();

    if (fields.contains("usePreset")) m_lora.usePreset = fields["usePreset"].toBool();
    if (fields.contains("modemPreset")) m_lora.modemPreset = fields["modemPreset"].toInt();
    if (fields.contains("region")) m_lora.region = fields["region"].toInt();
    if (fields.contains("hopLimit")) m_lora.hopLimit = fields["hopLimit"].toInt();
    if (fields.contains("txEnabled")) m_lora.txEnabled = fields["txEnabled"].toBool();
    if (fields.contains("txPower")) m_lora.txPower = fields["txPower"].toInt();
    if (fields.contains("channelNum")) m_lora.channelNum = fields["channelNum"].toInt();
    if (fields.contains("overrideDutyCycle")) m_lora.overrideDutyCycle = fields["overrideDutyCycle"].toBool();
    if (fields.contains("frequencyOffset")) m_lora.frequencyOffset = fields["frequencyOffset"].toFloat();
    if (fields.contains("bandwidth")) m_lora.bandwidth = fields["bandwidth"].toInt();
    if (fields.contains("spreadFactor")) m_lora.spreadFactor = fields["spreadFactor"].toInt();
    if (fields.contains("codingRate")) m_lora.codingRate = fields["codingRate"].toInt();

    m_hasLora = true;
    qDebug() << "DeviceConfig: LoRa config updated - region:" << m_lora.region
             << "preset:" << m_lora.modemPreset << "hopLimit:" << m_lora.hopLimit;
    emit loraConfigChanged();
}

void DeviceConfig::updateFromDevicePacket(const QVariantMap &fields)
{
    if (fields.contains("role")) m_device.role = fields["role"].toInt();
    if (fields.contains("serialEnabled")) m_device.serialEnabled = fields["serialEnabled"].toBool();
    if (fields.contains("debugLogEnabled")) m_device.debugLogEnabled = fields["debugLogEnabled"].toBool();
    if (fields.contains("buttonGpio")) m_device.buttonGpio = fields["buttonGpio"].toInt();
    if (fields.contains("buzzerGpio")) m_device.buzzerGpio = fields["buzzerGpio"].toInt();
    if (fields.contains("rebroadcastMode")) m_device.rebroadcastMode = fields["rebroadcastMode"].toInt();
    if (fields.contains("nodeInfoBroadcastSecs")) m_device.nodeInfoBroadcastSecs = fields["nodeInfoBroadcastSecs"].toInt();
    if (fields.contains("doubleTapAsButtonPress")) m_device.doubleTapAsButtonPress = fields["doubleTapAsButtonPress"].toBool();
    if (fields.contains("isManaged")) m_device.isManaged = fields["isManaged"].toBool();
    if (fields.contains("disableTripleClick")) m_device.disableTripleClick = fields["disableTripleClick"].toBool();
    if (fields.contains("tzdef")) m_device.tzdef = fields["tzdef"].toString();
    if (fields.contains("ledHeartbeatDisabled")) m_device.ledHeartbeatDisabled = fields["ledHeartbeatDisabled"].toBool();

    m_hasDevice = true;
    emit deviceConfigChanged();
}

void DeviceConfig::updateFromPositionPacket(const QVariantMap &fields)
{
    if (fields.contains("positionBroadcastSecs")) m_position.positionBroadcastSecs = fields["positionBroadcastSecs"].toInt();
    if (fields.contains("smartPositionEnabled")) m_position.smartPositionEnabled = fields["smartPositionEnabled"].toBool();
    if (fields.contains("fixedPosition")) m_position.fixedPosition = fields["fixedPosition"].toBool();
    if (fields.contains("gpsEnabled")) m_position.gpsEnabled = fields["gpsEnabled"].toBool();
    if (fields.contains("gpsUpdateInterval")) m_position.gpsUpdateInterval = fields["gpsUpdateInterval"].toInt();
    if (fields.contains("gpsAttemptTime")) m_position.gpsAttemptTime = fields["gpsAttemptTime"].toInt();
    if (fields.contains("positionFlags")) m_position.positionFlags = fields["positionFlags"].toInt();
    if (fields.contains("broadcastSmartMinDistance")) m_position.broadcastSmartMinDistance = fields["broadcastSmartMinDistance"].toInt();
    if (fields.contains("broadcastSmartMinIntervalSecs")) m_position.broadcastSmartMinIntervalSecs = fields["broadcastSmartMinIntervalSecs"].toInt();
    if (fields.contains("gpsMode")) m_position.gpsMode = fields["gpsMode"].toInt();

    m_hasPosition = true;
    emit positionConfigChanged();
}

void DeviceConfig::updateFromChannelPacket(const QVariantMap &fields)
{
    int index = fields.value("index", -1).toInt();
    if (index < 0 || index >= m_channels.size()) return;

    ChannelConfig &ch = m_channels[index];
    ch.index = index;
    if (fields.contains("role")) ch.role = fields["role"].toInt();
    if (fields.contains("name")) ch.name = fields["name"].toString();
    if (fields.contains("psk")) ch.psk = fields["psk"].toByteArray();
    if (fields.contains("uplinkEnabled")) ch.uplinkEnabled = fields["uplinkEnabled"].toBool();
    if (fields.contains("downlinkEnabled")) ch.downlinkEnabled = fields["downlinkEnabled"].toBool();

    emit channelConfigChanged(index);
}

QStringList DeviceConfig::regionNames()
{
    return QStringList{
        "Unset",
        "US",
        "EU_433",
        "EU_868",
        "CN",
        "JP",
        "ANZ",
        "KR",
        "TW",
        "RU",
        "IN",
        "NZ_865",
        "TH",
        "LORA_24",
        "UA_433",
        "UA_868",
        "MY_433",
        "MY_919",
        "SG_923"
    };
}

QStringList DeviceConfig::modemPresetNames()
{
    return QStringList{
        "Long Range - Fast",
        "Long Range - Slow",
        "Very Long Range - Slow",
        "Medium Range - Slow",
        "Medium Range - Fast",
        "Short Range - Slow",
        "Short Range - Fast",
        "Long Range - Moderate"
    };
}

QStringList DeviceConfig::deviceRoleNames()
{
    return QStringList{
        "Client",
        "Client Mute",
        "Router",
        "Router Client",
        "Repeater",
        "Tracker",
        "Sensor",
        "TAK",
        "Client Hidden",
        "Lost and Found",
        "TAK Tracker"
    };
}

QStringList DeviceConfig::gpsModeNames()
{
    return QStringList{
        "Disabled",
        "Enabled",
        "Not Present"
    };
}
