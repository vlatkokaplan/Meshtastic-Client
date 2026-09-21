#include "ChannelsConfigTab.h"
#include "Theme.h"
#include "DeviceConfig.h"

#include <QDebug>
#include <QSignalBlocker>
#include <QListWidgetItem>
#include <QBrush>
#include <QMessageBox>
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
    phLabel->setStyleSheet(Theme::statusLabelStyle());
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
    pskHint->setStyleSheet(Theme::mutedLabelStyle(10));
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
    m_statusLabel->setStyleSheet(Theme::statusLabelStyle());
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

    // Any edit marks the editor dirty so an incoming channel packet won't
    // overwrite it (see onChannelConfigChanged)
    connect(m_roleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { markEditorDirty(); });
    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &) { markEditorDirty(); });
    connect(m_pskEdit, &QLineEdit::textEdited, this, [this](const QString &) { markEditorDirty(); });
    connect(m_uplinkCheck, &QCheckBox::toggled, this, [this](bool) { markEditorDirty(); });
    connect(m_downlinkCheck, &QCheckBox::toggled, this, [this](bool) { markEditorDirty(); });
    bottomLayout->addWidget(m_saveButton);

    editorLayout->addLayout(bottomLayout);
    editorLayout->addStretch();

    m_stackedWidget->addWidget(m_editorWidget);

    mainLayout->addWidget(m_stackedWidget, 1);
}

void ChannelsConfigTab::updateChannelList()
{
    // Rebuilding this list used to call clear(), which emits
    // currentRowChanged(-1) and so tore down the selection and the editor every
    // time a channel arrived from the device - eight times during a config
    // dump, and again after every save. Update the labels in place instead, and
    // keep signals blocked so nothing observes a half-built list.
    const QSignalBlocker blocker(m_channelList);

    while (m_channelList->count() < 8)
        m_channelList->addItem(QString());
    while (m_channelList->count() > 8)
        delete m_channelList->takeItem(m_channelList->count() - 1);

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

        QListWidgetItem *item = m_channelList->item(i);
        if (item->text() != label)
            item->setText(label);

        // Disabled channels are dimmed so the configured ones stand out
        item->setForeground(QBrush(ch.role == 0 ? Theme::palette().textMuted
                                                : Theme::palette().text));
    }

    // Keep the row the user is on selected across rebuilds
    if (m_currentChannel >= 0 && m_currentChannel < 8
        && m_channelList->currentRow() != m_currentChannel)
    {
        m_channelList->setCurrentRow(m_currentChannel);
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
    m_editorDirty = false;
    m_stackedWidget->setCurrentWidget(m_editorWidget);
    updateEditorFromConfig(row);
}

void ChannelsConfigTab::markEditorDirty()
{
    if (m_currentChannel < 0 || m_editorDirty)
        return;
    m_editorDirty = true;
    m_statusLabel->setText(QString("Channel %1 - unsaved changes").arg(m_currentChannel));
    m_statusLabel->setStyleSheet(QString("color: %1;").arg(Theme::palette().warning.name()));
}

void ChannelsConfigTab::onChannelConfigChanged(int index)
{
    updateChannelList();

    if (index != m_currentChannel)
        return;

    // The device can send a channel while the user is editing it. Refreshing
    // the editor then would silently discard what they typed.
    if (m_editorDirty) {
        m_statusLabel->setText(
            QString("Channel %1 changed on the device - your unsaved edits are kept").arg(index));
        m_statusLabel->setStyleSheet(QString("color: %1;").arg(Theme::palette().warning.name()));
        return;
    }

    updateEditorFromConfig(index);
}

void ChannelsConfigTab::updateEditorFromConfig(int index)
{
    auto ch = m_config->channel(index);

    // Writing the fields below would otherwise look like user edits
    const QSignalBlocker b1(m_roleCombo), b2(m_nameEdit), b3(m_pskEdit),
                         b4(m_uplinkCheck), b5(m_downlinkCheck);

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

    m_editorDirty = false;
    m_statusLabel->setText(QString("Editing channel %1").arg(index));
    m_statusLabel->setStyleSheet(Theme::statusLabelStyle());
}

void ChannelsConfigTab::notifySaved()
{
    m_editorDirty = false;
    m_statusLabel->setText("Saved \u2713");
    m_statusLabel->setStyleSheet(QString("color: %1;").arg(Theme::palette().success.name()));
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

    // Convert base64 PSK back to bytes. fromBase64() silently returns junk for
    // malformed input, and the radio only accepts 0, 1, 16 or 32 byte keys, so
    // refuse anything else rather than writing a broken channel to the device.
    QString pskBase64 = m_pskEdit->text().trimmed();
    if (!pskBase64.isEmpty()) {
        auto decoded = QByteArray::fromBase64Encoding(pskBase64.toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) {
            QMessageBox::warning(this, "Invalid PSK",
                                 "The pre-shared key is not valid base64.\n\n"
                                 "Use \"Generate\" for a new random key, or paste the "
                                 "base64 key from another device.");
            m_statusLabel->setText("Invalid PSK - not saved");
            m_statusLabel->setStyleSheet(QString("color: %1;").arg(Theme::palette().danger.name()));
            return;
        }
        ch.psk = *decoded;

        const int n = ch.psk.size();
        if (n != 1 && n != 16 && n != 32) {
            QMessageBox::warning(this, "Invalid PSK",
                                 QString("A pre-shared key must be 1, 16 or 32 bytes; "
                                         "this one decodes to %1.\n\n"
                                         "1 byte selects a default key, 16 bytes is AES-128 "
                                         "and 32 bytes is AES-256.").arg(n));
            m_statusLabel->setText(QString("PSK is %1 bytes - not saved").arg(n));
            m_statusLabel->setStyleSheet(QString("color: %1;").arg(Theme::palette().danger.name()));
            return;
        }
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
    urlLabel->setStyleSheet(Theme::mutedLabelStyle(10) + "margin-top: 8px;");
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
