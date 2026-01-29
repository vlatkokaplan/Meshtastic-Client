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
#include <QBrush>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QToolTip>
#include <QHelpEvent>
#include <QShortcut>
#include <QKeySequence>
#include "AppSettings.h"

// Custom delegate to paint message items with their explicit colors and show tooltips
class MessageItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        if (event && event->type() == QEvent::ToolTip)
        {
            QString tooltip = index.data(Qt::ToolTipRole).toString();
            if (!tooltip.isEmpty())
            {
                QToolTip::showText(event->globalPos(), tooltip, view);
                return true;
            }
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();

        // Get the item's background and foreground colors
        QVariant bgVar = index.data(Qt::BackgroundRole);
        QVariant fgVar = index.data(Qt::ForegroundRole);

        QRect rect = option.rect;

        // Draw background
        if (bgVar.isValid())
        {
            QBrush bgBrush = bgVar.value<QBrush>();
            painter->fillRect(rect, bgBrush);
        }

        // Draw selection overlay if selected
        if (option.state & QStyle::State_Selected)
        {
            painter->fillRect(rect, QColor(0, 0, 0, 40));
        }

        // Draw text with explicit foreground color
        if (fgVar.isValid())
        {
            painter->setPen(fgVar.value<QBrush>().color());
        }
        else
        {
            painter->setPen(option.palette.text().color());
        }

        // Use the font from the option (which includes widget's font)
        painter->setFont(option.font);

        QString text = index.data(Qt::DisplayRole).toString();
        QRect textRect = rect.adjusted(4, 4, -4, -4);

        // Check alignment
        int alignment = index.data(Qt::TextAlignmentRole).toInt();
        if (alignment == 0)
            alignment = Qt::AlignLeft | Qt::AlignVCenter;

        painter->drawText(textRect, alignment | Qt::TextWordWrap, text);

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QString text = index.data(Qt::DisplayRole).toString();
        QFontMetrics fm(option.font);
        int width = option.rect.width() > 0 ? option.rect.width() - 8 : 300;
        QRect bound = fm.boundingRect(0, 0, width, 10000, Qt::TextWordWrap, text);
        return QSize(width, bound.height() + 16);
    }
};

MessagesWidget::MessagesWidget(NodeManager *nodeManager, QWidget *parent)
    : QWidget(parent), m_nodeManager(nodeManager), m_database(nullptr), m_fontSize(AppSettings::instance()->messageFontSize())
{
    setupUI();

    // Set up zoom shortcuts
    QShortcut *zoomInShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), this);
    connect(zoomInShortcut, &QShortcut::activated, this, &MessagesWidget::zoomIn);

    QShortcut *zoomInShortcut2 = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), this);
    connect(zoomInShortcut2, &QShortcut::activated, this, &MessagesWidget::zoomIn);

    QShortcut *zoomOutShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), this);
    connect(zoomOutShortcut, &QShortcut::activated, this, &MessagesWidget::zoomOut);

    QShortcut *zoomResetShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this);
    connect(zoomResetShortcut, &QShortcut::activated, this, &MessagesWidget::zoomReset);

    // Apply saved font size
    updateMessageFont();

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
            border-radius: 4px;
            font-size: 13px;
        }
        QTreeWidget::item {
            padding: 6px 4px;
            border-radius: 4px;
        }
        QTreeWidget::item:selected {
            background-color: #0084ff;
            color: white;
        }
        QTreeWidget::item:hover:!selected {
            background-color: rgba(128, 128, 128, 0.2);
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
    m_headerLabel->setStyleSheet("font-size: 14px; font-weight: bold; padding: 8px; border-radius: 4px;");
    rightLayout->addWidget(m_headerLabel);

    // Message list
    m_messageList = new QListWidget;
    m_messageList->setWordWrap(true);
    m_messageList->setSpacing(4);
    m_messageList->setStyleSheet(R"(
        QListWidget {
            border-radius: 6px;
        }
        QListWidget::item {
            padding: 10px 14px;
            border-radius: 10px;
            margin: 4px 8px;
        }
    )");
    m_messageList->setItemDelegate(new MessageItemDelegate(m_messageList));
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
    for (int i = 0; i < 8; i++)
    {
        ChannelInfo ch;
        ch.index = i;
        ch.name = (i == 0) ? "Primary" : QString("Channel %1").arg(i);
        ch.enabled = (i == 0); // Only primary enabled by default
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
    if (index < 0 || index > 7)
        return;

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
    for (int i = 0; i < 8; i++)
    {
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
    if (isDuplicate(msg))
    {
        return;
    }

    m_messages.append(msg);

    // Save to database
    if (m_database)
    {
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

void MessagesWidget::updateMessageStatus(uint32_t packetId, int errorReason)
{
    if (packetId == 0)
        return;

    qDebug() << "[MessagesWidget] Updating status for packet" << packetId << "with error reason" << errorReason;

    for (ChatMessage &msg : m_messages)
    {
        if (msg.packetId == packetId)
        {
            qDebug() << "[MessagesWidget] Found message with matching packetId, updating from status" << static_cast<int>(msg.status);
            // Map Routing_Error enum to MessageStatus
            switch (errorReason)
            {
            case 0: // NONE
                msg.status = MessageStatus::Sent;
                break;
            case 1: // NO_ROUTE
                msg.status = MessageStatus::NoRoute;
                break;
            case 2: // GOT_NAK
                msg.status = MessageStatus::GotNak;
                break;
            case 3: // TIMEOUT
                msg.status = MessageStatus::Timeout;
                break;
            case 5: // MAX_RETRANSMIT
                msg.status = MessageStatus::MaxRetransmit;
                break;
            case 8: // NO_RESPONSE
                msg.status = MessageStatus::NoResponse;
                break;
            default:
                msg.status = MessageStatus::Failed;
                break;
            }

            qDebug() << "[MessagesWidget] Status updated to" << static_cast<int>(msg.status);

            // Update display if this conversation is currently visible
            if ((m_currentType == ConversationType::DirectMessage &&
                 (msg.fromNode == m_currentDmNode || msg.toNode == m_currentDmNode)) ||
                (m_currentType == ConversationType::Channel && msg.channelIndex == m_currentChannel))
            {
                qDebug() << "[MessagesWidget] Refreshing message display";
                updateMessageDisplay();
            }
            else
            {
                qDebug() << "[MessagesWidget] Message not in current view, skipping display update";
            }
            return;
        }
    }
    qDebug() << "[MessagesWidget] No message found with packetId" << packetId;
}

void MessagesWidget::updateMessageDelivered(uint32_t packetId)
{
    if (packetId == 0)
        return;

    qDebug() << "[MessagesWidget] Marking delivery confirmation for packet" << packetId;

    for (ChatMessage &msg : m_messages)
    {
        if (msg.packetId == packetId)
        {
            // Only mark as delivered if it's a private message and currently at "Sent" status
            bool isPrivateMessage = (msg.toNode != 0xFFFFFFFF && msg.toNode != 0);
            if (isPrivateMessage && msg.status == MessageStatus::Sent)
            {
                msg.status = MessageStatus::Delivered;
                qDebug() << "[MessagesWidget] Message marked as delivered";

                // Update display if this conversation is currently visible
                if ((m_currentType == ConversationType::DirectMessage &&
                     (msg.fromNode == m_currentDmNode || msg.toNode == m_currentDmNode)) ||
                    (m_currentType == ConversationType::Channel && msg.channelIndex == m_currentChannel))
                {
                    qDebug() << "[MessagesWidget] Refreshing message display for delivery";
                    updateMessageDisplay();
                }
            }
            return;
        }
    }
    qDebug() << "[MessagesWidget] No message found with packetId" << packetId;
}

void MessagesWidget::loadFromDatabase()
{
    if (!m_database)
        return;

    m_messages.clear();

    QList<Database::Message> dbMessages = m_database->loadMessages(1000, 0);
    for (const Database::Message &dbMsg : dbMessages)
    {
        if (dbMsg.portNum != 1)
            continue;

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

    std::sort(m_messages.begin(), m_messages.end(), [](const ChatMessage &a, const ChatMessage &b)
              { return a.timestamp < b.timestamp; });

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

    for (auto it = m_channels.begin(); it != m_channels.end(); ++it)
    {
        const ChannelInfo &ch = it.value();
        if (!ch.enabled && ch.index != 0)
            continue; // Skip disabled channels except primary

        QTreeWidgetItem *item = new QTreeWidgetItem(channelsHeader);

        int unread = getUnreadCount(ConversationType::Channel, ch.index, 0);
        QString label = ch.name;
        if (unread > 0)
        {
            label += QString(" (%1)").arg(unread);
        }
        item->setText(0, label);
        item->setData(0, Qt::UserRole, ch.index);
        item->setData(0, Qt::UserRole + 1, static_cast<int>(ConversationType::Channel));

        // Icon based on channel
        if (ch.index == 0)
        {
            item->setIcon(0, QIcon::fromTheme("network-wireless"));
        }
        else
        {
            item->setIcon(0, QIcon::fromTheme("folder"));
        }
    }

    // Direct Messages section
    QSet<uint32_t> dmPartners = getDmPartners();
    if (!dmPartners.isEmpty())
    {
        QTreeWidgetItem *dmHeader = new QTreeWidgetItem(m_conversationTree);
        dmHeader->setText(0, "Direct Messages");
        dmHeader->setFlags(Qt::ItemIsEnabled);
        dmHeader->setExpanded(true);
        dmHeader->setFont(0, boldFont);

        for (uint32_t nodeNum : dmPartners)
        {
            QTreeWidgetItem *item = new QTreeWidgetItem(dmHeader);

            QString name = getNodeName(nodeNum);
            int unread = getUnreadCount(ConversationType::DirectMessage, 0, nodeNum);
            if (unread > 0)
            {
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
    if (!item || !item->parent())
        return; // Skip headers

    ConversationType type = static_cast<ConversationType>(item->data(0, Qt::UserRole + 1).toInt());

    if (type == ConversationType::Channel)
    {
        m_currentType = ConversationType::Channel;
        m_currentChannel = item->data(0, Qt::UserRole).toInt();
        m_currentDmNode = 0;

        QString channelName = m_channels.contains(m_currentChannel)
                                  ? m_channels[m_currentChannel].name
                                  : QString("Channel %1").arg(m_currentChannel);
        m_headerLabel->setText(QString("# %1").arg(channelName));
    }
    else if (type == ConversationType::DirectMessage)
    {
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
    if (text.isEmpty())
        return;

    if (m_currentType == ConversationType::Channel)
    {
        // Send to channel (broadcast)
        emit sendMessage(text, 0xFFFFFFFF, m_currentChannel);
    }
    else if (m_currentType == ConversationType::DirectMessage)
    {
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

    if (m_currentType == ConversationType::None)
    {
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();

    for (const ChatMessage &msg : m_messages)
    {
        bool show = false;

        if (m_currentType == ConversationType::Channel)
        {
            // Show messages on this channel that are broadcast
            show = (msg.channelIndex == m_currentChannel && msg.toNode == 0xFFFFFFFF);
        }
        else if (m_currentType == ConversationType::DirectMessage)
        {
            // Show DMs to/from this node
            bool isDm = (msg.toNode != 0xFFFFFFFF);
            bool involves = (msg.fromNode == m_currentDmNode || msg.toNode == m_currentDmNode);
            bool involvesMe = (msg.fromNode == myNode || msg.toNode == myNode);
            show = isDm && involves && involvesMe;
        }

        if (!show)
            continue;

        QListWidgetItem *item = new QListWidgetItem;
        item->setData(Qt::UserRole, QVariant::fromValue(msg.packetId)); // Store packet ID for reactions
        item->setData(Qt::UserRole + 1, QVariant::fromValue(msg.fromNode));
        item->setText(formatMessage(msg));

        // Add tooltip for message status (DMs only)
        if (msg.toNode != 0xFFFFFFFF && msg.isOutgoing)
        {
            QString tooltip;
            switch (msg.status)
            {
            case MessageStatus::Sending:
                tooltip = "Sending - waiting for response";
                break;
            case MessageStatus::Sent:
                tooltip = "Sent - delivered successfully";
                break;
            case MessageStatus::NoRoute:
                tooltip = "Failed - no route to destination";
                break;
            case MessageStatus::GotNak:
                tooltip = "Failed - received negative acknowledgment";
                break;
            case MessageStatus::Timeout:
                tooltip = "Failed - message timed out";
                break;
            case MessageStatus::MaxRetransmit:
                tooltip = "Failed - maximum retransmissions reached";
                break;
            case MessageStatus::NoResponse:
                tooltip = "Failed - no response from recipient";
                break;
            case MessageStatus::Failed:
                tooltip = "Failed - delivery error";
                break;
            }
            item->setToolTip(tooltip);
        }

        bool isOutgoing = (msg.fromNode == myNode);

        // Use explicit brush colors that override any theme/stylesheet
        if (isOutgoing)
        {
            item->setBackground(QBrush(QColor("#0084ff")));
            item->setForeground(QBrush(QColor(255, 255, 255))); // White text
            item->setTextAlignment(Qt::AlignRight);
        }
        else
        {
            item->setBackground(QBrush(QColor("#e4e6eb")));
            item->setForeground(QBrush(QColor(28, 30, 33))); // Dark text #1c1e21
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
                       ? ""
                       : msg.timestamp.toString("MMM d ");

    QString prefix = QString("[%1%2] %3:\n").arg(date, time, sender);

    // Add status icon for outgoing messages (channel and DM)
    QString statusIcon;
    if (msg.isOutgoing)
    {
        switch (msg.status)
        {
        case MessageStatus::Sending:
            statusIcon = "⏱ ";
            break;
        case MessageStatus::Sent:
            // Single tick for ACK from mesh
            statusIcon = "✓ ";
            break;
        case MessageStatus::Delivered:
            // Double tick for delivery confirmation (private messages only)
            statusIcon = "✓✓ ";
            break;
        case MessageStatus::NoRoute:
            statusIcon = "❌ ";
            break;
        case MessageStatus::GotNak:
            statusIcon = "⚠ ";
            break;
        case MessageStatus::Timeout:
            statusIcon = "⏰ ";
            break;
        case MessageStatus::MaxRetransmit:
            statusIcon = "🔁 ";
            break;
        case MessageStatus::NoResponse:
            statusIcon = "❓ ";
            break;
        case MessageStatus::Failed:
            statusIcon = "⛔ ";
            break;
        }
    }

    return prefix + msg.text + "\n" + statusIcon;
}

QString MessagesWidget::getNodeName(uint32_t nodeNum)
{
    if (nodeNum == 0xFFFFFFFF)
        return "Everyone";
    if (nodeNum == m_nodeManager->myNodeNum())
        return "You";

    NodeInfo node = m_nodeManager->getNode(nodeNum);
    if (!node.longName.isEmpty())
        return node.longName;
    if (!node.shortName.isEmpty())
        return node.shortName;
    if (!node.nodeId.isEmpty())
        return node.nodeId;

    return QString("!%1").arg(nodeNum, 8, 16, QChar('0'));
}

bool MessagesWidget::isDuplicate(const ChatMessage &msg)
{
    if (msg.packetId != 0)
    {
        for (const ChatMessage &existing : m_messages)
        {
            if (existing.packetId == msg.packetId)
            {
                return true;
            }
        }
    }

    for (const ChatMessage &existing : m_messages)
    {
        if (existing.fromNode == msg.fromNode &&
            existing.text == msg.text &&
            existing.channelIndex == msg.channelIndex &&
            qAbs(existing.timestamp.secsTo(msg.timestamp)) < 2)
        {
            return true;
        }
    }

    return false;
}

int MessagesWidget::getUnreadCount(ConversationType type, int channel, uint32_t nodeNum)
{
    int count = 0;
    uint32_t myNode = m_nodeManager->myNodeNum();

    for (const ChatMessage &msg : m_messages)
    {
        if (msg.read || msg.fromNode == myNode)
            continue;

        if (type == ConversationType::Channel)
        {
            if (msg.channelIndex == channel && msg.toNode == 0xFFFFFFFF)
            {
                count++;
            }
        }
        else if (type == ConversationType::DirectMessage)
        {
            if (msg.toNode != 0xFFFFFFFF &&
                (msg.fromNode == nodeNum || msg.toNode == nodeNum))
            {
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

    for (const ChatMessage &msg : m_messages)
    {
        if (msg.toNode == 0xFFFFFFFF)
            continue; // Skip broadcast

        if (msg.fromNode == myNode && msg.toNode != myNode)
        {
            partners.insert(msg.toNode);
        }
        else if (msg.toNode == myNode && msg.fromNode != myNode)
        {
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
    if (!item)
        return;

    uint32_t packetId = item->data(Qt::UserRole).toUInt();
    uint32_t fromNode = item->data(Qt::UserRole + 1).toUInt();

    QMenu menu(this);

    // Quick reactions
    QMenu *reactMenu = menu.addMenu("React");
    QStringList reactions = {"👍", "❤️", "😂", "😮", "😢", "🎉"};
    for (const QString &emoji : reactions)
    {
        QAction *action = reactMenu->addAction(emoji);
        connect(action, &QAction::triggered, this, [this, emoji, packetId]()
                { sendReactionToMessage(emoji, packetId); });
    }

    QAction *replyAction = menu.addAction("Reply");
    replyAction->setIcon(QIcon::fromTheme("mail-reply-sender"));

    menu.addSeparator();

    QAction *copyAction = menu.addAction("Copy Text");
    copyAction->setIcon(QIcon::fromTheme("edit-copy"));

    QAction *selected = menu.exec(m_messageList->viewport()->mapToGlobal(pos));

    if (selected == replyAction)
    {
        // Set up reply mode - prefix input with reply indicator
        m_replyToPacketId = packetId;
        m_replyToNode = fromNode;
        m_inputEdit->setPlaceholderText(QString("Replying to message..."));
        m_inputEdit->setFocus();
    }
    else if (selected == copyAction)
    {
        QString text = item->text();
        // Extract just the message part (after the newline)
        int newlinePos = text.indexOf('\n');
        if (newlinePos >= 0)
        {
            text = text.mid(newlinePos + 1);
        }
        QApplication::clipboard()->setText(text);
    }
}

void MessagesWidget::onConversationContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_conversationTree->itemAt(pos);
    if (!item || !item->parent())
        return; // Skip headers

    ConversationType type = static_cast<ConversationType>(item->data(0, Qt::UserRole + 1).toInt());

    if (type != ConversationType::DirectMessage)
        return; // Only DMs can be deleted

    uint32_t nodeNum = item->data(0, Qt::UserRole).toUInt();
    QString nodeName = getNodeName(nodeNum);

    QMenu menu(this);

    QAction *deleteAction = menu.addAction("Delete Conversation");
    deleteAction->setIcon(QIcon::fromTheme("edit-delete"));

    QAction *selected = menu.exec(m_conversationTree->viewport()->mapToGlobal(pos));

    if (selected == deleteAction)
    {
        // Confirm deletion
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Delete Conversation",
            QString("Delete all messages with %1?\n\nThis cannot be undone.").arg(nodeName),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes)
        {
            deleteConversation(nodeNum);
        }
    }
}

void MessagesWidget::sendReactionToMessage(const QString &emoji, uint32_t replyToId)
{
    if (m_currentType == ConversationType::Channel)
    {
        emit sendReaction(emoji, 0xFFFFFFFF, m_currentChannel, replyToId);
    }
    else if (m_currentType == ConversationType::DirectMessage)
    {
        emit sendReaction(emoji, m_currentDmNode, 0, replyToId);
    }
}

void MessagesWidget::deleteConversation(uint32_t nodeNum)
{
    // Remove messages from memory
    m_messages.erase(
        std::remove_if(m_messages.begin(), m_messages.end(),
                       [nodeNum, this](const ChatMessage &msg)
                       {
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
    if (m_database)
    {
        m_database->deleteMessagesWithNode(nodeNum);
    }

    // Clear selection if this was the current conversation
    if (m_currentType == ConversationType::DirectMessage && m_currentDmNode == nodeNum)
    {
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

void MessagesWidget::zoomIn()
{
    if (m_fontSize < 24)
    {
        m_fontSize++;
        AppSettings::instance()->setMessageFontSize(m_fontSize);
        updateMessageFont();
    }
}

void MessagesWidget::zoomOut()
{
    if (m_fontSize > 6)
    {
        m_fontSize--;
        AppSettings::instance()->setMessageFontSize(m_fontSize);
        updateMessageFont();
    }
}

void MessagesWidget::zoomReset()
{
    m_fontSize = 10;
    AppSettings::instance()->setMessageFontSize(m_fontSize);
    updateMessageFont();
}

void MessagesWidget::updateMessageFont()
{
    QFont font = m_messageList->font();
    font.setPointSize(m_fontSize);
    m_messageList->setFont(font);
}
