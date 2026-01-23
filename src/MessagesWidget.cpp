#include "MessagesWidget.h"
#include "NodeManager.h"
#include "Database.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QDateTime>
#include <QDebug>

MessagesWidget::MessagesWidget(NodeManager *nodeManager, QWidget *parent)
    : QWidget(parent)
    , m_nodeManager(nodeManager)
    , m_database(nullptr)
{
    setupUI();

    connect(m_nodeManager, &NodeManager::nodesChanged, this, [this]() {
        // Update node filter combo when nodes change
        uint32_t currentFilter = m_filterNode;
        m_nodeFilterCombo->clear();
        m_nodeFilterCombo->addItem("All Messages", 0);
        m_nodeFilterCombo->addItem("Broadcast", 0xFFFFFFFF);

        QList<NodeInfo> nodes = m_nodeManager->allNodes();
        for (const NodeInfo &node : nodes) {
            QString name = node.longName.isEmpty() ? node.shortName : node.longName;
            if (name.isEmpty()) name = node.nodeId;
            m_nodeFilterCombo->addItem(name, node.nodeNum);
        }

        // Restore selection
        for (int i = 0; i < m_nodeFilterCombo->count(); i++) {
            if (m_nodeFilterCombo->itemData(i).toUInt() == currentFilter) {
                m_nodeFilterCombo->setCurrentIndex(i);
                break;
            }
        }
    });
}

void MessagesWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Top bar with filters
    QHBoxLayout *filterLayout = new QHBoxLayout;

    QLabel *channelLabel = new QLabel("Channel:");
    m_channelCombo = new QComboBox;
    m_channelCombo->addItem("Primary (0)", 0);
    m_channelCombo->addItem("Channel 1", 1);
    m_channelCombo->addItem("Channel 2", 2);
    m_channelCombo->addItem("Channel 3", 3);
    m_channelCombo->addItem("Channel 4", 4);
    m_channelCombo->addItem("Channel 5", 5);
    m_channelCombo->addItem("Channel 6", 6);
    m_channelCombo->addItem("Channel 7", 7);
    m_channelCombo->addItem("All Channels", -1);
    connect(m_channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MessagesWidget::onChannelSelected);

    QLabel *filterLabel = new QLabel("Filter:");
    m_nodeFilterCombo = new QComboBox;
    m_nodeFilterCombo->addItem("All Messages", 0);
    m_nodeFilterCombo->setMinimumWidth(150);
    connect(m_nodeFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MessagesWidget::onNodeFilterChanged);

    filterLayout->addWidget(channelLabel);
    filterLayout->addWidget(m_channelCombo);
    filterLayout->addSpacing(20);
    filterLayout->addWidget(filterLabel);
    filterLayout->addWidget(m_nodeFilterCombo);
    filterLayout->addStretch();

    m_statusLabel = new QLabel;
    filterLayout->addWidget(m_statusLabel);

    mainLayout->addLayout(filterLayout);

    // Message list
    m_messageList = new QListWidget;
    m_messageList->setWordWrap(true);
    m_messageList->setSpacing(4);
    m_messageList->setStyleSheet(R"(
        QListWidget {
            background-color: #1a1a2e;
            border: 1px solid #333;
            border-radius: 4px;
        }
        QListWidget::item {
            padding: 8px;
            border-radius: 8px;
            margin: 2px 4px;
        }
    )");
    mainLayout->addWidget(m_messageList, 1);

    // Input area
    QHBoxLayout *inputLayout = new QHBoxLayout;

    m_inputEdit = new QLineEdit;
    m_inputEdit->setPlaceholderText("Type a message... (sending not yet implemented)");
    m_inputEdit->setEnabled(false);  // Sending not implemented yet
    connect(m_inputEdit, &QLineEdit::returnPressed, this, &MessagesWidget::onSendClicked);

    m_sendButton = new QPushButton("Send");
    m_sendButton->setEnabled(false);  // Sending not implemented yet
    connect(m_sendButton, &QPushButton::clicked, this, &MessagesWidget::onSendClicked);

    inputLayout->addWidget(m_inputEdit, 1);
    inputLayout->addWidget(m_sendButton);

    mainLayout->addLayout(inputLayout);
}

void MessagesWidget::setDatabase(Database *db)
{
    m_database = db;
}

void MessagesWidget::addMessage(const ChatMessage &msg)
{
    // Check for duplicates using packetId
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
        dbMsg.portNum = 1;  // TEXT_MESSAGE_APP
        m_database->saveMessage(dbMsg);
    }

    updateMessageDisplay();
    updateStatusLabel();
}

void MessagesWidget::loadFromDatabase()
{
    if (!m_database) return;

    m_messages.clear();

    QList<Database::Message> dbMessages = m_database->loadMessages(500, 0);
    for (const Database::Message &dbMsg : dbMessages) {
        if (dbMsg.portNum != 1) continue;  // Only text messages

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

    // Sort by timestamp (oldest first for display)
    std::sort(m_messages.begin(), m_messages.end(), [](const ChatMessage &a, const ChatMessage &b) {
        return a.timestamp < b.timestamp;
    });

    updateMessageDisplay();
    updateStatusLabel();

    qDebug() << "Loaded" << m_messages.size() << "messages from database";
}

void MessagesWidget::clear()
{
    m_messages.clear();
    m_messageList->clear();
    updateStatusLabel();
}

void MessagesWidget::onChannelSelected(int index)
{
    Q_UNUSED(index);
    m_currentChannel = m_channelCombo->currentData().toInt();
    updateMessageDisplay();
}

void MessagesWidget::onNodeFilterChanged(int index)
{
    Q_UNUSED(index);
    m_filterNode = m_nodeFilterCombo->currentData().toUInt();
    updateMessageDisplay();
}

void MessagesWidget::onSendClicked()
{
    QString text = m_inputEdit->text().trimmed();
    if (text.isEmpty()) return;

    // TODO: Implement sending
    // emit sendMessage(text, toNode, m_currentChannel);

    m_inputEdit->clear();
}

void MessagesWidget::refreshMessageList()
{
    updateMessageDisplay();
}

void MessagesWidget::updateMessageDisplay()
{
    m_messageList->clear();

    uint32_t myNode = m_nodeManager->myNodeNum();

    for (const ChatMessage &msg : m_messages) {
        // Filter by channel
        if (m_currentChannel >= 0 && msg.channelIndex != m_currentChannel) {
            continue;
        }

        // Filter by node
        if (m_filterNode != 0) {
            if (m_filterNode == 0xFFFFFFFF) {
                // Show only broadcast
                if (msg.toNode != 0xFFFFFFFF) continue;
            } else {
                // Show messages to/from specific node
                if (msg.fromNode != m_filterNode && msg.toNode != m_filterNode) continue;
            }
        }

        QListWidgetItem *item = new QListWidgetItem;
        item->setText(formatMessage(msg));

        bool isOutgoing = (msg.fromNode == myNode);

        if (isOutgoing) {
            item->setBackground(QColor("#0d47a1"));
            item->setForeground(Qt::white);
            item->setTextAlignment(Qt::AlignRight);
        } else {
            if (msg.toNode == 0xFFFFFFFF) {
                // Broadcast message
                item->setBackground(QColor("#2e7d32"));
            } else {
                // Direct message
                item->setBackground(QColor("#37474f"));
            }
            item->setForeground(Qt::white);
            item->setTextAlignment(Qt::AlignLeft);
        }

        m_messageList->addItem(item);
    }

    // Scroll to bottom
    m_messageList->scrollToBottom();
}

QString MessagesWidget::formatMessage(const ChatMessage &msg)
{
    QString sender = getNodeName(msg.fromNode);
    QString time = msg.timestamp.toString("hh:mm");
    QString date = msg.timestamp.date() == QDate::currentDate()
                   ? "" : msg.timestamp.toString("MMM d ");

    QString prefix;
    if (msg.toNode == 0xFFFFFFFF) {
        prefix = QString("[%1%2] %3 (broadcast):\n").arg(date, time, sender);
    } else if (msg.toNode == m_nodeManager->myNodeNum()) {
        prefix = QString("[%1%2] %3 -> you:\n").arg(date, time, sender);
    } else {
        QString recipient = getNodeName(msg.toNode);
        prefix = QString("[%1%2] %3 -> %4:\n").arg(date, time, sender, recipient);
    }

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
    // Check by packetId if available
    if (msg.packetId != 0) {
        for (const ChatMessage &existing : m_messages) {
            if (existing.packetId == msg.packetId) {
                return true;
            }
        }
    }

    // Also check by content + timestamp + sender (within 2 seconds)
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

void MessagesWidget::updateStatusLabel()
{
    int total = m_messages.size();
    int displayed = m_messageList->count();
    m_statusLabel->setText(QString("Messages: %1 (showing %2)").arg(total).arg(displayed));
}
