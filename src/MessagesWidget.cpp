#include "MessagesWidget.h"
#include "NodeManager.h"
#include "Database.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QDateTime>
#include <QDebug>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>

MessagesWidget::MessagesWidget(NodeManager *nodeManager, QWidget *parent)
    : QWidget(parent)
    , m_nodeManager(nodeManager)
    , m_database(nullptr)
{
    setupUI();

    // Update DM list when nodes change
    connect(m_nodeManager, &NodeManager::nodesChanged, this, &MessagesWidget::updateConversationList);
}

void MessagesWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_splitter = new QSplitter(Qt::Horizontal);

    // Left panel - conversation list
    QWidget *leftPanel = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(4, 4, 4, 4);

    m_conversationTree = new QTreeWidget;
    m_conversationTree->setHeaderHidden(true);
    m_conversationTree->setIndentation(12);
    m_conversationTree->setMinimumWidth(180);
    m_conversationTree->setStyleSheet(R"(
        QTreeWidget {
            background-color: #ffffff;
            border: 1px solid #ddd;
            border-radius: 4px;
            font-size: 13px;
        }
        QTreeWidget::item {
            padding: 6px 4px;
            border-radius: 4px;
            color: #1c1e21;
        }
        QTreeWidget::item:selected {
            background-color: #0084ff;
            color: white;
        }
        QTreeWidget::item:hover:!selected {
            background-color: #f0f2f5;
        }
    )");
    connect(m_conversationTree, &QTreeWidget::itemClicked,
            this, &MessagesWidget::onConversationSelected);
    m_conversationTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_conversationTree, &QTreeWidget::customContextMenuRequested,
            this, &MessagesWidget::onConversationContextMenu);

    leftLayout->addWidget(m_conversationTree);
    m_splitter->addWidget(leftPanel);

    // Right panel - messages
    QWidget *rightPanel = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(4, 4, 4, 4);

    // Header
    m_headerLabel = new QLabel("Select a channel or conversation");
    m_headerLabel->setStyleSheet("font-size: 14px; font-weight: bold; padding: 8px; color: #1c1e21; background-color: #f0f2f5; border-radius: 4px;");
    rightLayout->addWidget(m_headerLabel);

    // Message list
    m_messageList = new QListWidget;
    m_messageList->setWordWrap(true);
    m_messageList->setSpacing(4);
    m_messageList->setStyleSheet(R"(
        QListWidget {
            background-color: #f5f5f5;
            border: 1px solid #ddd;
            border-radius: 6px;
        }
        QListWidget::item {
            padding: 10px 14px;
            border-radius: 10px;
            margin: 4px 8px;
        }
        QListWidget::item:hover {
            background-color: rgba(0, 0, 0, 0.05);
        }
    )");
    m_messageList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_messageList, &QListWidget::customContextMenuRequested,
            this, &MessagesWidget::onMessageContextMenu);
    rightLayout->addWidget(m_messageList, 1);

    // Input area
    QHBoxLayout *inputLayout = new QHBoxLayout;

    m_inputEdit = new QLineEdit;
    m_inputEdit->setPlaceholderText("Type a message...");
    m_inputEdit->setEnabled(false);
    connect(m_inputEdit, &QLineEdit::returnPressed, this, &MessagesWidget::onSendClicked);

    m_sendButton = new QPushButton("Send");
    m_sendButton->setEnabled(false);
    connect(m_sendButton, &QPushButton::clicked, this, &MessagesWidget::onSendClicked);

    inputLayout->addWidget(m_inputEdit, 1);
    inputLayout->addWidget(m_sendButton);
    rightLayout->addLayout(inputLayout);

    // Status
    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet("color: #65676b; font-size: 11px;");
    rightLayout->addWidget(m_statusLabel);

    m_splitter->addWidget(rightPanel);
    m_splitter->setSizes({200, 600});

    mainLayout->addWidget(m_splitter);

    // Initialize with default channels
    for (int i = 0; i < 8; i++) {
        ChannelInfo ch;
        ch.index = i;
        ch.name = (i == 0) ? "Primary" : QString("Channel %1").arg(i);
        ch.enabled = (i == 0);  // Only primary enabled by default
        m_channels[i] = ch;
    }

    updateConversationList();
}

void MessagesWidget::setDatabase(Database *db)
{
    m_database = db;
}

void MessagesWidget::setChannel(int index, const QString &name, bool enabled)
{
    if (index < 0 || index > 7) return;

    ChannelInfo ch;
    ch.index = index;
    ch.name = name.isEmpty() ? QString("Channel %1").arg(index) : name;
    ch.enabled = enabled;
    m_channels[index] = ch;

    updateConversationList();
}

void MessagesWidget::clearChannels()
{
    m_channels.clear();
    for (int i = 0; i < 8; i++) {
        ChannelInfo ch;
        ch.index = i;
        ch.name = (i == 0) ? "Primary" : QString("Channel %1").arg(i);
        ch.enabled = (i == 0);
        m_channels[i] = ch;
    }
    updateConversationList();
}

void MessagesWidget::addMessage(const ChatMessage &msg)
{
    if (isDuplicate(msg)) {
        return;
    }

    m_messages.append(msg);

    // Save to database
    if (m_database) {
        Database::Message dbMsg;
        dbMsg.fromNode = msg.fromNode;
        dbMsg.toNode = msg.toNode;
        dbMsg.channel = QString::number(msg.channelIndex);
        dbMsg.text = msg.text;
        dbMsg.timestamp = msg.timestamp;
        dbMsg.portNum = 1;
        m_database->saveMessage(dbMsg);
    }

    updateConversationList();
    updateMessageDisplay();
    updateStatusLabel();
}

void MessagesWidget::loadFromDatabase()
{
    if (!m_database) return;

    m_messages.clear();

    QList<Database::Message> dbMessages = m_database->loadMessages(1000, 0);
    for (const Database::Message &dbMsg : dbMessages) {
        if (dbMsg.portNum != 1) continue;

        ChatMessage msg;
        msg.id = dbMsg.id;
        msg.fromNode = dbMsg.fromNode;
        msg.toNode = dbMsg.toNode;
        msg.channelIndex = dbMsg.channel.toInt();
        msg.text = dbMsg.text;
        msg.timestamp = dbMsg.timestamp;
        msg.read = dbMsg.read;
        msg.isOutgoing = (dbMsg.fromNode == m_nodeManager->myNodeNum());

        m_messages.append(msg);
    }

    std::sort(m_messages.begin(), m_messages.end(), [](const ChatMessage &a, const ChatMessage &b) {
        return a.timestamp < b.timestamp;
    });

    updateConversationList();
    updateMessageDisplay();
    updateStatusLabel();

    qDebug() << "Loaded" << m_messages.size() << "messages from database";
}

void MessagesWidget::clear()
{
    m_messages.clear();
    m_messageList->clear();
    m_currentType = ConversationType::None;
    m_currentChannel = -1;
    m_currentDmNode = 0;
    m_manualDmPartners.clear();
    m_inputEdit->setEnabled(false);
    m_sendButton->setEnabled(false);
    m_headerLabel->setText("Select a channel or conversation");
    clearChannels();
    updateStatusLabel();
}

void MessagesWidget::updateConversationList()
{
    m_conversationTree->clear();

    // Channels section
    QTreeWidgetItem *channelsHeader = new QTreeWidgetItem(m_conversationTree);
    channelsHeader->setText(0, "Channels");
    channelsHeader->setFlags(Qt::ItemIsEnabled);
    channelsHeader->setExpanded(true);
    QFont boldFont = channelsHeader->font(0);
    boldFont.setBold(true);
    channelsHeader->setFont(0, boldFont);

    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        const ChannelInfo &ch = it.value();
        if (!ch.enabled && ch.index != 0) continue;  // Skip disabled channels except primary

        QTreeWidgetItem *item = new QTreeWidgetItem(channelsHeader);

        int unread = getUnreadCount(ConversationType::Channel, ch.index, 0);
        QString label = ch.name;
        if (unread > 0) {
            label += QString(" (%1)").arg(unread);
        }
        item->setText(0, label);
        item->setData(0, Qt::UserRole, ch.index);
        item->setData(0, Qt::UserRole + 1, static_cast<int>(ConversationType::Channel));

        // Icon based on channel
        if (ch.index == 0) {
            item->setIcon(0, QIcon::fromTheme("network-wireless"));
        } else {
            item->setIcon(0, QIcon::fromTheme("folder"));
        }
    }

    // Direct Messages section
    QSet<uint32_t> dmPartners = getDmPartners();
    if (!dmPartners.isEmpty()) {
        QTreeWidgetItem *dmHeader = new QTreeWidgetItem(m_conversationTree);
        dmHeader->setText(0, "Direct Messages");
        dmHeader->setFlags(Qt::ItemIsEnabled);
        dmHeader->setExpanded(true);
        dmHeader->setFont(0, boldFont);

        for (uint32_t nodeNum : dmPartners) {
            QTreeWidgetItem *item = new QTreeWidgetItem(dmHeader);

            QString name = getNodeName(nodeNum);
            int unread = getUnreadCount(ConversationType::DirectMessage, 0, nodeNum);
            if (unread > 0) {
                name += QString(" (%1)").arg(unread);
            }
            item->setText(0, name);
            item->setData(0, Qt::UserRole, nodeNum);
            item->setData(0, Qt::UserRole + 1, static_cast<int>(ConversationType::DirectMessage));
            item->setIcon(0, QIcon::fromTheme("user-available"));
        }
    }

    m_conversationTree->expandAll();
}

void MessagesWidget::onConversationSelected(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item || !item->parent()) return;  // Skip headers

    ConversationType type = static_cast<ConversationType>(item->data(0, Qt::UserRole + 1).toInt());

    if (type == ConversationType::Channel) {
        m_currentType = ConversationType::Channel;
        m_currentChannel = item->data(0, Qt::UserRole).toInt();
        m_currentDmNode = 0;

        QString channelName = m_channels.contains(m_currentChannel)
            ? m_channels[m_currentChannel].name
            : QString("Channel %1").arg(m_currentChannel);
        m_headerLabel->setText(QString("# %1").arg(channelName));
    } else if (type == ConversationType::DirectMessage) {
        m_currentType = ConversationType::DirectMessage;
        m_currentChannel = -1;
        m_currentDmNode = item->data(0, Qt::UserRole).toUInt();

        m_headerLabel->setText(QString("DM with %1").arg(getNodeName(m_currentDmNode)));
    }

    updateMessageDisplay();

    // Enable input for sending
    m_inputEdit->setEnabled(true);
    m_sendButton->setEnabled(true);
    m_inputEdit->setFocus();
}

void MessagesWidget::onSendClicked()
{
    QString text = m_inputEdit->text().trimmed();
    if (text.isEmpty()) return;

    if (m_currentType == ConversationType::Channel) {
        // Send to channel (broadcast)
        emit sendMessage(text, 0xFFFFFFFF, m_currentChannel);
    } else if (m_currentType == ConversationType::DirectMessage) {
        // Send direct message to specific node
        emit sendMessage(text, m_currentDmNode, 0);
    }

    // Clear reply mode
    m_replyToPacketId = 0;
    m_replyToNode = 0;
    m_inputEdit->setPlaceholderText("Type a message...");
    m_inputEdit->clear();
}

void MessagesWidget::updateMessageDisplay()
{
    m_messageList->clear();

    if (m_currentType == ConversationType::None) {
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();

    for (const ChatMessage &msg : m_messages) {
        bool show = false;

        if (m_currentType == ConversationType::Channel) {
            // Show messages on this channel that are broadcast
            show = (msg.channelIndex == m_currentChannel && msg.toNode == 0xFFFFFFFF);
        } else if (m_currentType == ConversationType::DirectMessage) {
            // Show DMs to/from this node
            bool isDm = (msg.toNode != 0xFFFFFFFF);
            bool involves = (msg.fromNode == m_currentDmNode || msg.toNode == m_currentDmNode);
            bool involvesMe = (msg.fromNode == myNode || msg.toNode == myNode);
            show = isDm && involves && involvesMe;
        }

        if (!show) continue;

        QListWidgetItem *item = new QListWidgetItem;
        item->setData(Qt::UserRole, QVariant::fromValue(msg.packetId));  // Store packet ID for reactions
        item->setData(Qt::UserRole + 1, QVariant::fromValue(msg.fromNode));
        item->setText(formatMessage(msg));

        bool isOutgoing = (msg.fromNode == myNode);

        if (isOutgoing) {
            item->setBackground(QColor("#0084ff"));  // Messenger-style blue
            item->setForeground(QColor("#ffffff"));
            item->setTextAlignment(Qt::AlignRight);
        } else {
            if (m_currentType == ConversationType::Channel) {
                item->setBackground(QColor("#e4e6eb"));  // Light gray for channel
                item->setForeground(QColor("#1c1e21"));
            } else {
                item->setBackground(QColor("#e4e6eb"));  // Light gray for DM
                item->setForeground(QColor("#1c1e21"));
            }
            item->setTextAlignment(Qt::AlignLeft);
        }

        m_messageList->addItem(item);
    }

    m_messageList->scrollToBottom();
    updateStatusLabel();
}

QString MessagesWidget::formatMessage(const ChatMessage &msg)
{
    QString sender = getNodeName(msg.fromNode);
    QString time = msg.timestamp.toString("hh:mm");
    QString date = msg.timestamp.date() == QDate::currentDate()
                   ? "" : msg.timestamp.toString("MMM d ");

    QString prefix = QString("[%1%2] %3:\n").arg(date, time, sender);
    return prefix + msg.text;
}

QString MessagesWidget::getNodeName(uint32_t nodeNum)
{
    if (nodeNum == 0xFFFFFFFF) return "Everyone";
    if (nodeNum == m_nodeManager->myNodeNum()) return "You";

    NodeInfo node = m_nodeManager->getNode(nodeNum);
    if (!node.longName.isEmpty()) return node.longName;
    if (!node.shortName.isEmpty()) return node.shortName;
    if (!node.nodeId.isEmpty()) return node.nodeId;

    return QString("!%1").arg(nodeNum, 8, 16, QChar('0'));
}

bool MessagesWidget::isDuplicate(const ChatMessage &msg)
{
    if (msg.packetId != 0) {
        for (const ChatMessage &existing : m_messages) {
            if (existing.packetId == msg.packetId) {
                return true;
            }
        }
    }

    for (const ChatMessage &existing : m_messages) {
        if (existing.fromNode == msg.fromNode &&
            existing.text == msg.text &&
            existing.channelIndex == msg.channelIndex &&
            qAbs(existing.timestamp.secsTo(msg.timestamp)) < 2) {
            return true;
        }
    }

    return false;
}

int MessagesWidget::getUnreadCount(ConversationType type, int channel, uint32_t nodeNum)
{
    int count = 0;
    uint32_t myNode = m_nodeManager->myNodeNum();

    for (const ChatMessage &msg : m_messages) {
        if (msg.read || msg.fromNode == myNode) continue;

        if (type == ConversationType::Channel) {
            if (msg.channelIndex == channel && msg.toNode == 0xFFFFFFFF) {
                count++;
            }
        } else if (type == ConversationType::DirectMessage) {
            if (msg.toNode != 0xFFFFFFFF &&
                (msg.fromNode == nodeNum || msg.toNode == nodeNum)) {
                count++;
            }
        }
    }

    return count;
}

QSet<uint32_t> MessagesWidget::getDmPartners()
{
    QSet<uint32_t> partners;
    uint32_t myNode = m_nodeManager->myNodeNum();

    // Include manually started DM conversations
    partners = m_manualDmPartners;

    for (const ChatMessage &msg : m_messages) {
        if (msg.toNode == 0xFFFFFFFF) continue;  // Skip broadcast

        if (msg.fromNode == myNode && msg.toNode != myNode) {
            partners.insert(msg.toNode);
        } else if (msg.toNode == myNode && msg.fromNode != myNode) {
            partners.insert(msg.fromNode);
        }
    }

    return partners;
}

void MessagesWidget::updateStatusLabel()
{
    int total = m_messages.size();
    int displayed = m_messageList->count();
    m_statusLabel->setText(QString("Total: %1 messages | Showing: %2").arg(total).arg(displayed));
}

void MessagesWidget::onMessageContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_messageList->itemAt(pos);
    if (!item) return;

    uint32_t packetId = item->data(Qt::UserRole).toUInt();
    uint32_t fromNode = item->data(Qt::UserRole + 1).toUInt();

    QMenu menu(this);

    // Quick reactions
    QMenu *reactMenu = menu.addMenu("React");
    QStringList reactions = {"👍", "❤️", "😂", "😮", "😢", "🎉"};
    for (const QString &emoji : reactions) {
        QAction *action = reactMenu->addAction(emoji);
        connect(action, &QAction::triggered, this, [this, emoji, packetId]() {
            sendReactionToMessage(emoji, packetId);
        });
    }

    QAction *replyAction = menu.addAction("Reply");
    replyAction->setIcon(QIcon::fromTheme("mail-reply-sender"));

    menu.addSeparator();

    QAction *copyAction = menu.addAction("Copy Text");
    copyAction->setIcon(QIcon::fromTheme("edit-copy"));

    QAction *selected = menu.exec(m_messageList->viewport()->mapToGlobal(pos));

    if (selected == replyAction) {
        // Set up reply mode - prefix input with reply indicator
        m_replyToPacketId = packetId;
        m_replyToNode = fromNode;
        m_inputEdit->setPlaceholderText(QString("Replying to message..."));
        m_inputEdit->setFocus();
    } else if (selected == copyAction) {
        QString text = item->text();
        // Extract just the message part (after the newline)
        int newlinePos = text.indexOf('\n');
        if (newlinePos >= 0) {
            text = text.mid(newlinePos + 1);
        }
        QApplication::clipboard()->setText(text);
    }
}

void MessagesWidget::onConversationContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_conversationTree->itemAt(pos);
    if (!item || !item->parent()) return;  // Skip headers

    ConversationType type = static_cast<ConversationType>(item->data(0, Qt::UserRole + 1).toInt());

    if (type != ConversationType::DirectMessage) return;  // Only DMs can be deleted

    uint32_t nodeNum = item->data(0, Qt::UserRole).toUInt();
    QString nodeName = getNodeName(nodeNum);

    QMenu menu(this);

    QAction *deleteAction = menu.addAction("Delete Conversation");
    deleteAction->setIcon(QIcon::fromTheme("edit-delete"));

    QAction *selected = menu.exec(m_conversationTree->viewport()->mapToGlobal(pos));

    if (selected == deleteAction) {
        // Confirm deletion
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Delete Conversation",
            QString("Delete all messages with %1?\n\nThis cannot be undone.").arg(nodeName),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            deleteConversation(nodeNum);
        }
    }
}

void MessagesWidget::sendReactionToMessage(const QString &emoji, uint32_t replyToId)
{
    if (m_currentType == ConversationType::Channel) {
        emit sendReaction(emoji, 0xFFFFFFFF, m_currentChannel, replyToId);
    } else if (m_currentType == ConversationType::DirectMessage) {
        emit sendReaction(emoji, m_currentDmNode, 0, replyToId);
    }
}

void MessagesWidget::deleteConversation(uint32_t nodeNum)
{
    // Remove messages from memory
    m_messages.erase(
        std::remove_if(m_messages.begin(), m_messages.end(),
            [nodeNum, this](const ChatMessage &msg) {
                uint32_t myNode = m_nodeManager->myNodeNum();
                bool isDm = (msg.toNode != 0xFFFFFFFF);
                bool involves = (msg.fromNode == nodeNum || msg.toNode == nodeNum);
                bool involvesMe = (msg.fromNode == myNode || msg.toNode == myNode);
                return isDm && involves && involvesMe;
            }),
        m_messages.end());

    // Remove from manual DM partners
    m_manualDmPartners.remove(nodeNum);

    // Delete from database
    if (m_database) {
        m_database->deleteMessagesWithNode(nodeNum);
    }

    // Clear selection if this was the current conversation
    if (m_currentType == ConversationType::DirectMessage && m_currentDmNode == nodeNum) {
        m_currentType = ConversationType::None;
        m_currentDmNode = 0;
        m_headerLabel->setText("Select a channel or conversation");
        m_messageList->clear();
        m_inputEdit->setEnabled(false);
        m_sendButton->setEnabled(false);
    }

    updateConversationList();
    updateStatusLabel();
}

void MessagesWidget::startDirectMessage(uint32_t nodeNum)
{
    // Set up for DM with this node
    m_currentType = ConversationType::DirectMessage;
    m_currentChannel = -1;
    m_currentDmNode = nodeNum;

    // Add to manual DM partners so it appears in the list
    m_manualDmPartners.insert(nodeNum);

    m_headerLabel->setText(QString("DM with %1").arg(getNodeName(nodeNum)));

    // Make sure this node appears in the DM list
    updateConversationList();

    // Find and select the DM item in the tree
    for (int i = 0; i < m_conversationTree->topLevelItemCount(); i++)
    {
        QTreeWidgetItem *header = m_conversationTree->topLevelItem(i);
        if (header->text(0) == "Direct Messages")
        {
            for (int j = 0; j < header->childCount(); j++)
            {
                QTreeWidgetItem *item = header->child(j);
                if (item->data(0, Qt::UserRole).toUInt() == nodeNum)
                {
                    m_conversationTree->setCurrentItem(item);
                    break;
                }
            }
            break;
        }
    }

    // If node wasn't in DM list, we still set it up for messaging
    updateMessageDisplay();

    // Enable input
    m_inputEdit->setEnabled(true);
    m_sendButton->setEnabled(true);
    m_inputEdit->setFocus();
}
