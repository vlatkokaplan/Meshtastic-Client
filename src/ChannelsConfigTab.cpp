#include "ChannelsConfigTab.h"
#include "DeviceConfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QRandomGenerator>

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
    if (m_currentChannel < 0) return;

    DeviceConfig::ChannelConfig ch;
    ch.index = m_currentChannel;
    ch.role = m_roleCombo->currentIndex();
    ch.name = m_nameEdit->text();

    // Convert base64 PSK back to bytes
    QString pskBase64 = m_pskEdit->text().trimmed();
    if (!pskBase64.isEmpty()) {
        ch.psk = QByteArray::fromBase64(pskBase64.toLatin1());
    }

    ch.uplinkEnabled = m_uplinkCheck->isChecked();
    ch.downlinkEnabled = m_downlinkCheck->isChecked();

    m_config->setChannel(m_currentChannel, ch);

    m_statusLabel->setText("Saving...");
    m_statusLabel->setStyleSheet("color: orange;");

    emit saveRequested(m_currentChannel);
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
