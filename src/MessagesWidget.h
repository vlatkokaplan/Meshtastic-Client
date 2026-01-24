#ifndef MESSAGESWIDGET_H
#define MESSAGESWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QDateTime>
#include <QMap>
#include <QTreeWidget>

class NodeManager;
class Database;

struct ChannelInfo {
    int index = 0;
    QString name;
    QString psk;  // Not displayed, just stored
    bool enabled = false;
};

struct ChatMessage {
    qint64 id = 0;
    uint32_t fromNode = 0;
    uint32_t toNode = 0;
    int channelIndex = 0;
    QString text;
    QDateTime timestamp;
    bool isOutgoing = false;
    bool read = false;
    uint32_t packetId = 0;
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
    void startDirectMessage(uint32_t nodeNum);

    // Channel management
    void setChannel(int index, const QString &name, bool enabled);
    void clearChannels();

signals:
    void sendMessage(const QString &text, uint32_t toNode, int channel);
    void sendReaction(const QString &emoji, uint32_t toNode, int channel, uint32_t replyId);

private slots:
    void onConversationSelected(QTreeWidgetItem *item, int column);
    void onSendClicked();
    void onMessageContextMenu(const QPoint &pos);
    void onConversationContextMenu(const QPoint &pos);

private:
    NodeManager *m_nodeManager;
    Database *m_database;

    // UI components
    QSplitter *m_splitter;
    QTreeWidget *m_conversationTree;
    QListWidget *m_messageList;
    QLineEdit *m_inputEdit;
    QPushButton *m_sendButton;
    QLabel *m_headerLabel;
    QLabel *m_statusLabel;

    // Data
    QList<ChatMessage> m_messages;
    QMap<int, ChannelInfo> m_channels;

    // Current selection
    enum class ConversationType { None, Channel, DirectMessage };
    ConversationType m_currentType = ConversationType::None;
    int m_currentChannel = -1;
    uint32_t m_currentDmNode = 0;

    // Manually started DM conversations (nodes we want to message but haven't yet)
    QSet<uint32_t> m_manualDmPartners;

    // Reply mode
    uint32_t m_replyToPacketId = 0;
    uint32_t m_replyToNode = 0;

    void setupUI();
    void updateConversationList();
    void updateMessageDisplay();
    void updateStatusLabel();
    QString formatMessage(const ChatMessage &msg);
    QString getNodeName(uint32_t nodeNum);
    bool isDuplicate(const ChatMessage &msg);
    int getUnreadCount(ConversationType type, int channel, uint32_t nodeNum);
    QSet<uint32_t> getDmPartners();
    void sendReactionToMessage(const QString &emoji, uint32_t replyToId);
    void deleteConversation(uint32_t nodeNum);
};

#endif // MESSAGESWIDGET_H
