#ifndef CHANNELSCONFIGTAB_H
#define CHANNELSCONFIGTAB_H

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>

class DeviceConfig;

class ChannelsConfigTab : public QWidget
{
    Q_OBJECT

public:
    explicit ChannelsConfigTab(DeviceConfig *config, QWidget *parent = nullptr);

public slots:
    void notifySaved();

signals:
    void saveRequested(int channelIndex);

private slots:
    void onChannelSelected(int row);
    void onChannelConfigChanged(int index);
    void onSaveClicked();
    void onGeneratePskClicked();
    void onShareUrlClicked();
    void onShowQrClicked();

private:
    DeviceConfig *m_config;

    // UI elements
    QListWidget *m_channelList;
    QStackedWidget *m_stackedWidget;

    // Per-channel editor widgets
    QWidget *m_editorWidget;
    QLabel *m_channelIndexLabel;
    QComboBox *m_roleCombo;
    QLineEdit *m_nameEdit;
    QLineEdit *m_pskEdit;
    QPushButton *m_generatePskButton;
    QCheckBox *m_uplinkCheck;
    QCheckBox *m_downlinkCheck;
    QPushButton *m_saveButton;
    QPushButton *m_shareUrlButton;
    QPushButton *m_showQrButton;
    QLabel *m_statusLabel;

    // Placeholder for no selection
    QWidget *m_placeholderWidget;

    int m_currentChannel = -1;

    void setupUI();
    void updateChannelList();
    void updateEditorFromConfig(int index);
    QString roleToString(int role);
    QString buildChannelUrl() const;
};

#endif // CHANNELSCONFIGTAB_H
