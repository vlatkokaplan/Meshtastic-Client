#ifndef MESSAGESWIDGET_H
#define MESSAGESWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QSplitter>
#include <QDateTime>

class NodeManager;
class Database;

struct ChatMessage {
    qint64 id = 0;
    uint32_t fromNode = 0;
    uint32_t toNode = 0;
    QString channelName;
    int channelIndex = 0;
    QString text;
    QDateTime timestamp;
    bool isOutgoing = false;
    bool read = false;
    uint32_t packetId = 0;  // To avoid duplicates
};

class MessagesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MessagesWidget(NodeManager *nodeManager, QWidget *parent = nullptr);

    void setDatabase(Database *db);
    void addMessage(const ChatMessage &msg);
    void loadFromDatabase();
    void clear();

signals:
    void sendMessage(const QString &text, uint32_t toNode, int channel);

private slots:
    void onChannelSelected(int index);
    void onSendClicked();
    void onNodeFilterChanged(int index);
    void refreshMessageList();

private:
    NodeManager *m_nodeManager;
    Database *m_database;

    // UI components
    QComboBox *m_channelCombo;
    QComboBox *m_nodeFilterCombo;
    QListWidget *m_messageList;
    QLineEdit *m_inputEdit;
    QPushButton *m_sendButton;
    QLabel *m_statusLabel;

    // Data
    QList<ChatMessage> m_messages;
    int m_currentChannel = 0;
    uint32_t m_filterNode = 0;  // 0 = show all

    void setupUI();
    void updateMessageDisplay();
    void updateStatusLabel();
    QString formatMessage(const ChatMessage &msg);
    QString getNodeName(uint32_t nodeNum);
    bool isDuplicate(const ChatMessage &msg);
};

#endif // MESSAGESWIDGET_H
