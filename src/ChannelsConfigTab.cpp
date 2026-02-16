#include "ChannelsConfigTab.h"
#include "DeviceConfig.h"

#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QRandomGenerator>
#include <QClipboard>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPixmap>
#include <QImage>

#include <qrencode.h>

#include "meshtastic/apponly.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/config.pb.h"

ChannelsConfigTab::ChannelsConfigTab(DeviceConfig *config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
    setupUI();

    connect(m_config, &DeviceConfig::channelConfigChanged,
            this, &ChannelsConfigTab::onChannelConfigChanged);

    updateChannelList();
}

void ChannelsConfigTab::setupUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);

    // Left side: channel list
    QVBoxLayout *listLayout = new QVBoxLayout;

    QLabel *listLabel = new QLabel("Channels");
    listLabel->setStyleSheet("font-weight: bold;");
    listLayout->addWidget(listLabel);

    m_channelList = new QListWidget;
    m_channelList->setMaximumWidth(200);
    connect(m_channelList, &QListWidget::currentRowChanged,
            this, &ChannelsConfigTab::onChannelSelected);
    listLayout->addWidget(m_channelList);

    mainLayout->addLayout(listLayout);

    // Right side: stacked widget for editor/placeholder
    m_stackedWidget = new QStackedWidget;

    // Placeholder widget
    m_placeholderWidget = new QWidget;
    QVBoxLayout *phLayout = new QVBoxLayout(m_placeholderWidget);
    QLabel *phLabel = new QLabel("Select a channel to edit");
    phLabel->setAlignment(Qt::AlignCenter);
    phLabel->setStyleSheet("color: gray;");
    phLayout->addWidget(phLabel);
    m_stackedWidget->addWidget(m_placeholderWidget);

    // Editor widget
    m_editorWidget = new QWidget;
    QVBoxLayout *editorLayout = new QVBoxLayout(m_editorWidget);

    // Channel info group
    QGroupBox *infoGroup = new QGroupBox("Channel Settings");
    QFormLayout *infoLayout = new QFormLayout(infoGroup);

    m_channelIndexLabel = new QLabel;
    m_channelIndexLabel->setStyleSheet("font-weight: bold;");
    infoLayout->addRow("Channel:", m_channelIndexLabel);

    m_roleCombo = new QComboBox;
    m_roleCombo->addItems({"Disabled", "Primary", "Secondary"});
    m_roleCombo->setToolTip("Channel role: Disabled, Primary (main channel), or Secondary");
    infoLayout->addRow("Role:", m_roleCombo);

    m_nameEdit = new QLineEdit;
    m_nameEdit->setMaxLength(11);
    m_nameEdit->setPlaceholderText("Channel name (max 11 chars)");
    m_nameEdit->setToolTip("Short name for this channel");
    infoLayout->addRow("Name:", m_nameEdit);

    editorLayout->addWidget(infoGroup);

    // PSK group
    QGroupBox *pskGroup = new QGroupBox("Encryption Key (PSK)");
    QVBoxLayout *pskLayout = new QVBoxLayout(pskGroup);

    QHBoxLayout *pskInputLayout = new QHBoxLayout;
    m_pskEdit = new QLineEdit;
    m_pskEdit->setPlaceholderText("Base64 encoded key or 'AQ==' for default");
    m_pskEdit->setToolTip("Pre-shared key for channel encryption");
    pskInputLayout->addWidget(m_pskEdit);

    m_generatePskButton = new QPushButton("Generate");
    m_generatePskButton->setToolTip("Generate a random 256-bit key");
    connect(m_generatePskButton, &QPushButton::clicked,
            this, &ChannelsConfigTab::onGeneratePskClicked);
    pskInputLayout->addWidget(m_generatePskButton);

    pskLayout->addLayout(pskInputLayout);

    QLabel *pskHint = new QLabel("Use 'AQ==' for the default key, or generate a unique key for private channels.");
    pskHint->setWordWrap(true);
    pskHint->setStyleSheet("color: gray; font-size: 10px;");
    pskLayout->addWidget(pskHint);

    editorLayout->addWidget(pskGroup);

    // MQTT group
    QGroupBox *mqttGroup = new QGroupBox("MQTT Gateway");
    QVBoxLayout *mqttLayout = new QVBoxLayout(mqttGroup);

    m_uplinkCheck = new QCheckBox("Uplink Enabled");
    m_uplinkCheck->setToolTip("Send messages from this channel to MQTT");
    mqttLayout->addWidget(m_uplinkCheck);

    m_downlinkCheck = new QCheckBox("Downlink Enabled");
    m_downlinkCheck->setToolTip("Receive messages from MQTT to this channel");
    mqttLayout->addWidget(m_downlinkCheck);

    editorLayout->addWidget(mqttGroup);

    // Status and Save
    QHBoxLayout *bottomLayout = new QHBoxLayout;

    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet("color: gray;");
    bottomLayout->addWidget(m_statusLabel);

    bottomLayout->addStretch();

    m_shareUrlButton = new QPushButton("Share URL");
    m_shareUrlButton->setToolTip("Copy channel configuration URL to clipboard");
    connect(m_shareUrlButton, &QPushButton::clicked, this, &ChannelsConfigTab::onShareUrlClicked);
    bottomLayout->addWidget(m_shareUrlButton);

    m_showQrButton = new QPushButton("Show QR");
    m_showQrButton->setToolTip("Display QR code for channel configuration");
    connect(m_showQrButton, &QPushButton::clicked, this, &ChannelsConfigTab::onShowQrClicked);
    bottomLayout->addWidget(m_showQrButton);

    m_saveButton = new QPushButton("Save Channel");
    connect(m_saveButton, &QPushButton::clicked, this, &ChannelsConfigTab::onSaveClicked);
    bottomLayout->addWidget(m_saveButton);

    editorLayout->addLayout(bottomLayout);
    editorLayout->addStretch();

    m_stackedWidget->addWidget(m_editorWidget);

    mainLayout->addWidget(m_stackedWidget, 1);
}

void ChannelsConfigTab::updateChannelList()
{
    m_channelList->clear();

    for (int i = 0; i < 8; i++) {
        auto ch = m_config->channel(i);
        QString label;
        if (ch.role == 0) {
            label = QString("Channel %1 (Disabled)").arg(i);
        } else {
            QString name = ch.name.isEmpty() ? QString("Channel %1").arg(i) : ch.name;
            QString roleStr = (ch.role == 1) ? "Primary" : "Secondary";
            label = QString("%1 (%2)").arg(name, roleStr);
        }
        m_channelList->addItem(label);
    }
}

void ChannelsConfigTab::onChannelSelected(int row)
{
    if (row < 0 || row >= 8) {
        m_stackedWidget->setCurrentWidget(m_placeholderWidget);
        m_currentChannel = -1;
        return;
    }

    m_currentChannel = row;
    m_stackedWidget->setCurrentWidget(m_editorWidget);
    updateEditorFromConfig(row);
}

void ChannelsConfigTab::onChannelConfigChanged(int index)
{
    updateChannelList();
    if (index == m_currentChannel) {
        updateEditorFromConfig(index);
    }
}

void ChannelsConfigTab::updateEditorFromConfig(int index)
{
    auto ch = m_config->channel(index);

    m_channelIndexLabel->setText(QString("Channel %1").arg(index));
    m_roleCombo->setCurrentIndex(ch.role);
    m_nameEdit->setText(ch.name);

    // Convert PSK to base64 for display
    if (!ch.psk.isEmpty()) {
        m_pskEdit->setText(QString::fromLatin1(ch.psk.toBase64()));
    } else {
        m_pskEdit->clear();
    }

    m_uplinkCheck->setChecked(ch.uplinkEnabled);
    m_downlinkCheck->setChecked(ch.downlinkEnabled);

    m_statusLabel->setText(QString("Editing channel %1").arg(index));
    m_statusLabel->setStyleSheet("color: gray;");
}

void ChannelsConfigTab::onSaveClicked()
{
    qDebug() << "=== ChannelsConfigTab::onSaveClicked ===";
    qDebug() << "Current channel:" << m_currentChannel;

    if (m_currentChannel < 0) {
        qDebug() << "No channel selected!";
        return;
    }

    // Save channel index before setChannel() which may trigger list update and reset m_currentChannel
    int channelToSave = m_currentChannel;

    DeviceConfig::ChannelConfig ch;
    ch.index = channelToSave;
    ch.role = m_roleCombo->currentIndex();
    ch.name = m_nameEdit->text();

    qDebug() << "Saving - role:" << ch.role << "name:" << ch.name;

    // Convert base64 PSK back to bytes
    QString pskBase64 = m_pskEdit->text().trimmed();
    if (!pskBase64.isEmpty()) {
        ch.psk = QByteArray::fromBase64(pskBase64.toLatin1());
    }
    qDebug() << "PSK size:" << ch.psk.size();

    ch.uplinkEnabled = m_uplinkCheck->isChecked();
    ch.downlinkEnabled = m_downlinkCheck->isChecked();

    m_config->setChannel(channelToSave, ch);

    m_statusLabel->setText("Saving...");
    m_statusLabel->setStyleSheet("color: orange;");

    qDebug() << "Emitting saveRequested for channel" << channelToSave;
    emit saveRequested(channelToSave);
}

void ChannelsConfigTab::onGeneratePskClicked()
{
    // Generate a random 256-bit (32 byte) key
    QByteArray key(32, 0);
    for (int i = 0; i < 32; i++) {
        key[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    m_pskEdit->setText(QString::fromLatin1(key.toBase64()));
}

QString ChannelsConfigTab::roleToString(int role)
{
    switch (role) {
    case 0: return "Disabled";
    case 1: return "Primary";
    case 2: return "Secondary";
    default: return "Unknown";
    }
}

QString ChannelsConfigTab::buildChannelUrl() const
{
    // Build ChannelSet protobuf from DeviceConfig
    meshtastic::ChannelSet channelSet;

    // Add all enabled channels
    for (int i = 0; i < 8; i++) {
        auto ch = m_config->channel(i);
        if (ch.role == 0)
            continue; // skip disabled

        auto *settings = channelSet.add_settings();
        settings->set_channel_num(i);
        if (!ch.psk.isEmpty()) {
            settings->set_psk(ch.psk.constData(), ch.psk.size());
        }
        if (!ch.name.isEmpty()) {
            settings->set_name(ch.name.toStdString());
        }
        settings->set_uplink_enabled(ch.uplinkEnabled);
        settings->set_downlink_enabled(ch.downlinkEnabled);
    }

    // Add LoRa config
    if (m_config->hasLoRaConfig()) {
        auto lora = m_config->loraConfig();
        auto *loraProto = channelSet.mutable_lora_config();
        loraProto->set_use_preset(lora.usePreset);
        loraProto->set_modem_preset(lora.modemPreset);
        loraProto->set_region(lora.region);
        loraProto->set_hop_limit(lora.hopLimit);
        loraProto->set_tx_enabled(lora.txEnabled);
        loraProto->set_tx_power(lora.txPower);
        loraProto->set_bandwidth(lora.bandwidth);
        loraProto->set_spread_factor(lora.spreadFactor);
        loraProto->set_coding_rate(lora.codingRate);
    }

    // Serialize and base64url encode
    std::string serialized;
    channelSet.SerializeToString(&serialized);
    QByteArray data(serialized.data(), serialized.size());

    // Base64url encoding (replace + with -, / with _, strip padding =)
    QString base64 = QString::fromLatin1(data.toBase64());
    base64.replace('+', '-');
    base64.replace('/', '_');
    while (base64.endsWith('='))
        base64.chop(1);

    return QString("https://meshtastic.org/e/#") + base64;
}

void ChannelsConfigTab::onShareUrlClicked()
{
    QString url = buildChannelUrl();
    QApplication::clipboard()->setText(url);
    m_statusLabel->setText("Channel URL copied to clipboard");
    m_statusLabel->setStyleSheet("color: green;");
}

void ChannelsConfigTab::onShowQrClicked()
{
    QString url = buildChannelUrl();

    // Generate QR code using libqrencode
    QRcode *qr = QRcode_encodeString(url.toUtf8().constData(), 0, QR_ECLEVEL_M, QR_MODE_8, 1);
    if (!qr) {
        m_statusLabel->setText("Failed to generate QR code");
        m_statusLabel->setStyleSheet("color: red;");
        return;
    }

    // Convert to QImage (scale up for visibility)
    int scale = 6;
    int border = 2;
    int size = (qr->width + border * 2) * scale;
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(Qt::white);

    for (int y = 0; y < qr->width; y++) {
        for (int x = 0; x < qr->width; x++) {
            if (qr->data[y * qr->width + x] & 1) {
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        image.setPixel((x + border) * scale + dx,
                                       (y + border) * scale + dy,
                                       qRgb(0, 0, 0));
                    }
                }
            }
        }
    }
    QRcode_free(qr);

    // Show in dialog
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle("Channel QR Code");
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *layout = new QVBoxLayout(dialog);

    QLabel *qrLabel = new QLabel;
    qrLabel->setPixmap(QPixmap::fromImage(image));
    qrLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(qrLabel);

    QLabel *urlLabel = new QLabel(url);
    urlLabel->setWordWrap(true);
    urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    urlLabel->setStyleSheet("color: gray; font-size: 10px; margin-top: 8px;");
    layout->addWidget(urlLabel);

    QPushButton *copyButton = new QPushButton("Copy URL");
    connect(copyButton, &QPushButton::clicked, this, [url]() {
        QApplication::clipboard()->setText(url);
    });
    layout->addWidget(copyButton);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    dialog->exec();
}
