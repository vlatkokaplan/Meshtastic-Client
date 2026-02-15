#include "Database.h"
#include "MessagesWidget.h" // For ChatMessage struct
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QUuid>

static const int SCHEMA_VERSION = 7;

Database::Database(QObject *parent)
    : QObject(parent)
{
    m_connectionName = QUuid::createUuid().toString();
}

Database::~Database()
{
    close();
}

bool Database::open(const QString &path)
{
    QString dbPath = path;
    if (dbPath.isEmpty())
    {
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        dbPath = dataDir + "/meshtastic.db";
    }

    qDebug() << "Opening database at:" << dbPath;

    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open())
    {
        qWarning() << "Failed to open database:" << m_db.lastError().text();
        return false;
    }

    // Enable foreign keys
    QSqlQuery query(m_db);
    if (!query.exec("PRAGMA foreign_keys = ON"))
    {
        qWarning() << "Failed to enable foreign keys:" << query.lastError().text();
    }

    // Create or migrate tables
    int version = getSchemaVersion();
    if (version < SCHEMA_VERSION)
    {
        if (version == 0)
        {
            if (!createTables())
            {
                return false;
            }
        }
        else
        {
            if (!migrateSchema(version, SCHEMA_VERSION))
            {
                return false;
            }
        }
        setSchemaVersion(SCHEMA_VERSION);
    }

    if (!prepareStatements())
    {
        qWarning() << "Failed to prepare database statements";
        return false;
    }

    qDebug() << "Database opened successfully, schema version:" << SCHEMA_VERSION;
    return true;
}

void Database::close()
{
    cleanupStatements();
    if (m_db.isOpen())
    {
        m_db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool Database::isOpen() const
{
    return m_db.isOpen();
}

bool Database::createTables()
{
    QSqlQuery query(m_db);

    // Schema version table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER PRIMARY KEY
        )
    )"))
    {
        qWarning() << "Failed to create schema_version table:" << query.lastError().text();
        return false;
    }

    // Nodes table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS nodes (
            node_num INTEGER PRIMARY KEY,
            node_id TEXT,
            long_name TEXT,
            short_name TEXT,
            hw_model TEXT,
            latitude REAL,
            longitude REAL,
            altitude INTEGER,
            has_position INTEGER DEFAULT 0,
            battery_level INTEGER DEFAULT 0,
            voltage REAL DEFAULT 0,
            channel_utilization REAL DEFAULT 0,
            air_util_tx REAL DEFAULT 0,
            snr REAL DEFAULT 0,
            rssi INTEGER DEFAULT 0,
            hops_away INTEGER DEFAULT -1,
            is_external_power INTEGER DEFAULT 0,
            last_heard INTEGER,
            first_seen INTEGER,
            updated_at INTEGER
        )
    )"))
    {
        qWarning() << "Failed to create nodes table:" << query.lastError().text();
        return false;
    }

    // Ensure columns exist (for older databases)
    query.exec("ALTER TABLE nodes ADD COLUMN is_external_power INTEGER DEFAULT 0");
    query.exec("ALTER TABLE nodes ADD COLUMN temperature REAL DEFAULT 0");
    query.exec("ALTER TABLE nodes ADD COLUMN relative_humidity REAL DEFAULT 0");
    query.exec("ALTER TABLE nodes ADD COLUMN barometric_pressure REAL DEFAULT 0");
    query.exec("ALTER TABLE nodes ADD COLUMN uptime_seconds INTEGER DEFAULT 0");

    // Messages table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            from_node INTEGER,
            to_node INTEGER,
            channel TEXT,
            port_num INTEGER,
            text TEXT,
            payload BLOB,
            timestamp INTEGER,
            read INTEGER DEFAULT 0,
            created_at INTEGER,
            status INTEGER DEFAULT 0,
            packet_id INTEGER DEFAULT 0,
            FOREIGN KEY (from_node) REFERENCES nodes(node_num)
        )
    )"))
    {
        qWarning() << "Failed to create messages table:" << query.lastError().text();
        return false;
    }

    // Traceroutes table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS traceroutes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            from_node INTEGER,
            to_node INTEGER,
            route_to TEXT,
            route_back TEXT,
            snr_to TEXT,
            snr_back TEXT,
            timestamp INTEGER,
            is_response INTEGER DEFAULT 0,
            FOREIGN KEY (from_node) REFERENCES nodes(node_num),
            FOREIGN KEY (to_node) REFERENCES nodes(node_num)
        )
    )"))
    {
        qWarning() << "Failed to create traceroutes table:" << query.lastError().text();
        return false;
    }

    // Telemetry history table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS telemetry_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            node_num INTEGER,
            timestamp INTEGER,
            temperature REAL DEFAULT 0,
            humidity REAL DEFAULT 0,
            pressure REAL DEFAULT 0,
            battery_level INTEGER DEFAULT 0,
            voltage REAL DEFAULT 0,
            snr REAL DEFAULT 0,
            rssi INTEGER DEFAULT 0,
            channel_util REAL DEFAULT 0,
            air_util_tx REAL DEFAULT 0,
            FOREIGN KEY (node_num) REFERENCES nodes(node_num)
        )
    )"))
    {
        qWarning() << "Failed to create telemetry_history table:" << query.lastError().text();
        return false;
    }

    // Position history table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS position_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            node_num INTEGER,
            latitude REAL,
            longitude REAL,
            altitude INTEGER,
            timestamp INTEGER,
            FOREIGN KEY (node_num) REFERENCES nodes(node_num)
        )
    )"))
    {
        qWarning() << "Failed to create position_history table:" << query.lastError().text();
        return false;
    }

    // Raw packets table (for long-running sessions)
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS packets (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp INTEGER,
            packet_type INTEGER,
            from_node INTEGER,
            to_node INTEGER,
            port_num INTEGER,
            channel INTEGER,
            type_name TEXT,
            raw_data BLOB,
            fields_json TEXT
        )
    )"))
    {
        qWarning() << "Failed to create packets table:" << query.lastError().text();
        return false;
    }

    // Indexes
    query.exec("CREATE INDEX IF NOT EXISTS idx_messages_from ON messages(from_node)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_messages_to ON messages(to_node)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_messages_timestamp ON messages(timestamp DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_nodes_last_heard ON nodes(last_heard DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_traceroutes_timestamp ON traceroutes(timestamp DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_traceroutes_from ON traceroutes(from_node)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_traceroutes_to ON traceroutes(to_node)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_telemetry_node ON telemetry_history(node_num)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_telemetry_timestamp ON telemetry_history(timestamp DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_position_node ON position_history(node_num)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_position_timestamp ON position_history(timestamp DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_packets_timestamp ON packets(timestamp DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_packets_from ON packets(from_node)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_packets_type ON packets(packet_type)");

    return true;
}

bool Database::migrateSchema(int fromVersion, int toVersion)
{
    QSqlQuery query(m_db);

    for (int v = fromVersion + 1; v <= toVersion; v++)
    {
        switch (v)
        {
        case 2:
            // Add is_external_power column
            if (!query.exec("ALTER TABLE nodes ADD COLUMN is_external_power INTEGER DEFAULT 0"))
            {
                qWarning() << "Migration to v2 failed:" << query.lastError().text();
                return false;
            }
            qDebug() << "Database migrated to schema version 2";
            break;
        case 3:
        {
            // Add environment telemetry columns (ignore "duplicate column" errors)
            auto addColumnIfNotExists = [&](const QString &sql, const QString &colName) -> bool {
                if (!query.exec(sql))
                {
                    QString err = query.lastError().text().toLower();
                    // SQLite returns "duplicate column name" if column exists
                    if (err.contains("duplicate column"))
                    {
                        qDebug() << colName << "column already exists, skipping";
                        return true;
                    }
                    qWarning() << "Failed to add" << colName << "column:" << query.lastError().text();
                    return false;
                }
                return true;
            };

            if (!addColumnIfNotExists("ALTER TABLE nodes ADD COLUMN temperature REAL DEFAULT 0", "temperature"))
                return false;
            if (!addColumnIfNotExists("ALTER TABLE nodes ADD COLUMN relative_humidity REAL DEFAULT 0", "relative_humidity"))
                return false;
            if (!addColumnIfNotExists("ALTER TABLE nodes ADD COLUMN barometric_pressure REAL DEFAULT 0", "barometric_pressure"))
                return false;
            if (!addColumnIfNotExists("ALTER TABLE nodes ADD COLUMN uptime_seconds INTEGER DEFAULT 0", "uptime_seconds"))
                return false;

            qDebug() << "Database migrated to schema version 3";
            break;
        }
        case 4:
            // Add message status and packet_id columns - these are CRITICAL for v4
            qDebug() << "Migrating to schema version 4 - adding message columns";
            if (!query.exec("ALTER TABLE messages ADD COLUMN status INTEGER DEFAULT 0"))
            {
                qWarning() << "Failed to add status column:" << query.lastError().text();
                if (!query.exec("ALTER TABLE messages ADD COLUMN status INTEGER"))
                    qWarning() << "Still failed to add status column:" << query.lastError().text();
            }
            if (!query.exec("ALTER TABLE messages ADD COLUMN packet_id INTEGER DEFAULT 0"))
            {
                qWarning() << "Failed to add packet_id column:" << query.lastError().text();
                if (!query.exec("ALTER TABLE messages ADD COLUMN packet_id INTEGER"))
                    qWarning() << "Still failed to add packet_id column:" << query.lastError().text();
            }
            qDebug() << "Database migrated to schema version 4";
            break;
        case 5:
            // Add telemetry history table
            qDebug() << "Migrating to schema version 5 - adding telemetry_history table";
            if (!query.exec(R"(
                CREATE TABLE IF NOT EXISTS telemetry_history (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    node_num INTEGER,
                    timestamp INTEGER,
                    temperature REAL DEFAULT 0,
                    humidity REAL DEFAULT 0,
                    pressure REAL DEFAULT 0,
                    battery_level INTEGER DEFAULT 0,
                    voltage REAL DEFAULT 0,
                    snr REAL DEFAULT 0,
                    rssi INTEGER DEFAULT 0,
                    channel_util REAL DEFAULT 0,
                    air_util_tx REAL DEFAULT 0,
                    FOREIGN KEY (node_num) REFERENCES nodes(node_num)
                )
            )"))
            {
                qWarning() << "Migration to v5 failed:" << query.lastError().text();
                return false;
            }
            query.exec("CREATE INDEX IF NOT EXISTS idx_telemetry_node ON telemetry_history(node_num)");
            query.exec("CREATE INDEX IF NOT EXISTS idx_telemetry_timestamp ON telemetry_history(timestamp DESC)");
            qDebug() << "Database migrated to schema version 5";
            break;
        case 6:
            // Add position history table
            qDebug() << "Migrating to schema version 6 - adding position_history table";
            if (!query.exec(R"(
                CREATE TABLE IF NOT EXISTS position_history (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    node_num INTEGER,
                    latitude REAL,
                    longitude REAL,
                    altitude INTEGER,
                    timestamp INTEGER,
                    FOREIGN KEY (node_num) REFERENCES nodes(node_num)
                )
            )"))
            {
                qWarning() << "Migration to v6 failed:" << query.lastError().text();
                return false;
            }
            query.exec("CREATE INDEX IF NOT EXISTS idx_position_node ON position_history(node_num)");
            query.exec("CREATE INDEX IF NOT EXISTS idx_position_timestamp ON position_history(timestamp DESC)");
            qDebug() << "Database migrated to schema version 6";
            break;

        case 7:
            // Add packets table for long-running sessions
            qDebug() << "Migrating to schema version 7 - adding packets table";
            if (!query.exec(R"(
                CREATE TABLE IF NOT EXISTS packets (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp INTEGER,
                    packet_type INTEGER,
                    from_node INTEGER,
                    to_node INTEGER,
                    port_num INTEGER,
                    channel INTEGER,
                    type_name TEXT,
                    raw_data BLOB,
                    fields_json TEXT
                )
            )"))
            {
                qWarning() << "Migration to v7 failed:" << query.lastError().text();
                return false;
            }
            query.exec("CREATE INDEX IF NOT EXISTS idx_packets_timestamp ON packets(timestamp DESC)");
            query.exec("CREATE INDEX IF NOT EXISTS idx_packets_from ON packets(from_node)");
            query.exec("CREATE INDEX IF NOT EXISTS idx_packets_type ON packets(packet_type)");
            qDebug() << "Database migrated to schema version 7";
            break;
        }
    }
    return true;
}

int Database::getSchemaVersion()
{
    QSqlQuery query(m_db);
    if (query.exec("SELECT version FROM schema_version LIMIT 1") && query.next())
    {
        return query.value(0).toInt();
    }
    return 0;
}

void Database::setSchemaVersion(int version)
{
    QSqlQuery query(m_db);
    if (!query.exec("DELETE FROM schema_version"))
    {
        qWarning() << "Failed to clear schema_version:" << query.lastError().text();
    }
    query.prepare("INSERT INTO schema_version (version) VALUES (?)");
    query.addBindValue(version);
    if (!query.exec())
    {
        qWarning() << "Failed to set schema version:" << query.lastError().text();
    }
}

bool Database::prepareStatements()
{
    m_saveNodeStmt = new QSqlQuery(m_db);
    if (!m_saveNodeStmt->prepare(R"(
        INSERT OR REPLACE INTO nodes (
            node_num, node_id, long_name, short_name, hw_model,
            latitude, longitude, altitude, has_position,
            battery_level, voltage, channel_utilization, air_util_tx,
            snr, rssi, hops_away, is_external_power,
            temperature, relative_humidity, barometric_pressure, uptime_seconds,
            last_heard, first_seen, updated_at
        ) VALUES (
            :node_num, :node_id, :long_name, :short_name, :hw_model,
            :latitude, :longitude, :altitude, :has_position,
            :battery_level, :voltage, :channel_utilization, :air_util_tx,
            :snr, :rssi, :hops_away, :is_external_power,
            :temperature, :relative_humidity, :barometric_pressure, :uptime_seconds,
            :last_heard,
            COALESCE((SELECT first_seen FROM nodes WHERE node_num = :node_num2), :now),
            :updated_at
        )
    )"))
    {
        qWarning() << "Failed to prepare saveNode statement:" << m_saveNodeStmt->lastError().text();
        cleanupStatements();
        return false;
    }

    m_saveMessageStmt = new QSqlQuery(m_db);
    if (!m_saveMessageStmt->prepare(R"(
        INSERT INTO messages (from_node, to_node, channel, port_num, text, payload, timestamp, read, created_at, status, packet_id)
        VALUES (:from, :to, :channel, :port_num, :text, :payload, :timestamp, :read, :created_at, :status, :packet_id)
    )"))
    {
        qWarning() << "Failed to prepare saveMessage statement:" << m_saveMessageStmt->lastError().text();
        cleanupStatements();
        return false;
    }

    return true;
}

void Database::cleanupStatements()
{
    delete m_saveNodeStmt;
    m_saveNodeStmt = nullptr;
    delete m_saveMessageStmt;
    m_saveMessageStmt = nullptr;
}

bool Database::saveNode(const NodeInfo &node)
{
    if (node.nodeNum == 0)
        return false;

    if (!m_db.isOpen())
    {
        qWarning() << "Database not open in saveNode";
        return false;
    }

    QSqlQuery *query = m_saveNodeStmt;
    QSqlQuery fallback(m_db);
    if (!query)
    {
        fallback.prepare(R"(
            INSERT OR REPLACE INTO nodes (
                node_num, node_id, long_name, short_name, hw_model,
                latitude, longitude, altitude, has_position,
                battery_level, voltage, channel_utilization, air_util_tx,
                snr, rssi, hops_away, is_external_power,
                temperature, relative_humidity, barometric_pressure, uptime_seconds,
                last_heard, first_seen, updated_at
            ) VALUES (
                :node_num, :node_id, :long_name, :short_name, :hw_model,
                :latitude, :longitude, :altitude, :has_position,
                :battery_level, :voltage, :channel_utilization, :air_util_tx,
                :snr, :rssi, :hops_away, :is_external_power,
                :temperature, :relative_humidity, :barometric_pressure, :uptime_seconds,
                :last_heard,
                COALESCE((SELECT first_seen FROM nodes WHERE node_num = :node_num2), :now),
                :updated_at
            )
        )");
        query = &fallback;
    }

    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 lastHeard = node.lastHeard.isValid() ? node.lastHeard.toSecsSinceEpoch() : 0;

    query->bindValue(":node_num", node.nodeNum);
    query->bindValue(":node_num2", node.nodeNum);
    query->bindValue(":node_id", node.nodeId);
    query->bindValue(":long_name", node.longName);
    query->bindValue(":short_name", node.shortName);
    query->bindValue(":hw_model", node.hwModel);
    query->bindValue(":latitude", node.latitude);
    query->bindValue(":longitude", node.longitude);
    query->bindValue(":altitude", node.altitude);
    query->bindValue(":has_position", node.hasPosition ? 1 : 0);
    query->bindValue(":battery_level", node.batteryLevel);
    query->bindValue(":voltage", node.voltage);
    query->bindValue(":channel_utilization", node.channelUtilization);
    query->bindValue(":air_util_tx", node.airUtilTx);
    query->bindValue(":snr", node.snr);
    query->bindValue(":rssi", node.rssi);
    query->bindValue(":hops_away", node.hopsAway);
    query->bindValue(":is_external_power", node.isExternalPower ? 1 : 0);
    query->bindValue(":temperature", node.temperature);
    query->bindValue(":relative_humidity", node.relativeHumidity);
    query->bindValue(":barometric_pressure", node.barometricPressure);
    query->bindValue(":uptime_seconds", node.uptimeSeconds);
    query->bindValue(":last_heard", lastHeard);
    query->bindValue(":now", now);
    query->bindValue(":updated_at", now);

    if (!query->exec())
    {
        qWarning() << "Failed to save node:" << query->lastError().text();
        return false;
    }

    return true;
}

bool Database::saveNodes(const QList<NodeInfo> &nodes)
{
    if (nodes.isEmpty())
        return true;  // Nothing to save

    if (!m_db.isOpen())
    {
        qWarning() << "Database not open in saveNodes";
        return false;
    }

    if (!m_db.transaction())
    {
        qWarning() << "Failed to start transaction:" << m_db.lastError().text();
        return false;
    }

    for (const NodeInfo &node : nodes)
    {
        if (!saveNode(node))
        {
            m_db.rollback();
            return false;
        }
    }
    return m_db.commit();
}

NodeInfo Database::loadNode(uint32_t nodeNum)
{
    NodeInfo node;
    if (!m_db.isOpen())
    {
        qWarning() << "Database not open in loadNode";
        return node;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM nodes WHERE node_num = ?");
    query.addBindValue(nodeNum);

    if (query.exec() && query.next())
    {
        node.nodeNum = query.value("node_num").toUInt();
        node.nodeId = query.value("node_id").toString();
        node.longName = query.value("long_name").toString();
        node.shortName = query.value("short_name").toString();
        node.hwModel = query.value("hw_model").toString();
        node.latitude = query.value("latitude").toDouble();
        node.longitude = query.value("longitude").toDouble();
        node.altitude = query.value("altitude").toInt();
        node.hasPosition = query.value("has_position").toBool();
        node.batteryLevel = query.value("battery_level").toInt();
        node.voltage = query.value("voltage").toFloat();
        node.channelUtilization = query.value("channel_utilization").toFloat();
        node.airUtilTx = query.value("air_util_tx").toFloat();
        node.snr = query.value("snr").toFloat();
        node.rssi = query.value("rssi").toInt();
        node.hopsAway = query.value("hops_away").toInt();
        node.isExternalPower = query.value("is_external_power").toBool();
        node.temperature = query.value("temperature").toFloat();
        node.relativeHumidity = query.value("relative_humidity").toFloat();
        node.barometricPressure = query.value("barometric_pressure").toFloat();
        node.uptimeSeconds = query.value("uptime_seconds").toUInt();
        node.hasEnvironmentTelemetry = (node.temperature != 0.0f || node.relativeHumidity != 0.0f || node.barometricPressure != 0.0f);
        qint64 lastHeard = query.value("last_heard").toLongLong();
        if (lastHeard > 0)
        {
            node.lastHeard = QDateTime::fromSecsSinceEpoch(lastHeard);
        }
    }

    return node;
}

QList<NodeInfo> Database::loadAllNodes()
{
    QList<NodeInfo> nodes;
    if (!m_db.isOpen())
    {
        qWarning() << "Database not open in loadAllNodes";
        return nodes;
    }

    QSqlQuery query(m_db);

    if (!query.exec("SELECT * FROM nodes ORDER BY last_heard DESC"))
    {
        qWarning() << "Failed to load nodes:" << query.lastError().text();
        return nodes;
    }

    while (query.next())
    {
        NodeInfo node;
        node.nodeNum = query.value("node_num").toUInt();
        node.nodeId = query.value("node_id").toString();
        node.longName = query.value("long_name").toString();
        node.shortName = query.value("short_name").toString();
        node.hwModel = query.value("hw_model").toString();
        node.latitude = query.value("latitude").toDouble();
        node.longitude = query.value("longitude").toDouble();
        node.altitude = query.value("altitude").toInt();
        node.hasPosition = query.value("has_position").toBool();
        node.batteryLevel = query.value("battery_level").toInt();
        node.voltage = query.value("voltage").toFloat();
        node.channelUtilization = query.value("channel_utilization").toFloat();
        node.airUtilTx = query.value("air_util_tx").toFloat();
        node.snr = query.value("snr").toFloat();
        node.rssi = query.value("rssi").toInt();
        node.hopsAway = query.value("hops_away").toInt();
        node.isExternalPower = query.value("is_external_power").toBool();
        node.temperature = query.value("temperature").toFloat();
        node.relativeHumidity = query.value("relative_humidity").toFloat();
        node.barometricPressure = query.value("barometric_pressure").toFloat();
        node.uptimeSeconds = query.value("uptime_seconds").toUInt();
        node.hasEnvironmentTelemetry = (node.temperature != 0.0f || node.relativeHumidity != 0.0f || node.barometricPressure != 0.0f);
        qint64 lastHeard = query.value("last_heard").toLongLong();
        if (lastHeard > 0)
        {
            node.lastHeard = QDateTime::fromSecsSinceEpoch(lastHeard);
        }
        nodes.append(node);
    }

    qDebug() << "Loaded" << nodes.size() << "nodes from database";
    return nodes;
}

bool Database::deleteNode(uint32_t nodeNum)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM nodes WHERE node_num = ?");
    query.addBindValue(nodeNum);
    return query.exec();
}

int Database::nodeCount()
{
    QSqlQuery query(m_db);
    if (query.exec("SELECT COUNT(*) FROM nodes") && query.next())
    {
        return query.value(0).toInt();
    }
    return 0;
}

// Message operations

bool Database::saveMessage(const Message &msg)
{
    if (!m_db.isOpen())
    {
        qWarning() << "Database not open in saveMessage";
        return false;
    }

    QSqlQuery query(m_db);

    if (!query.prepare(R"(
        INSERT INTO messages (from_node, to_node, channel, port_num, text, payload, timestamp, read, created_at, status, packet_id)
        VALUES (:from, :to, :channel, :port_num, :text, :payload, :timestamp, :read, :created_at, :status, :packet_id)
    )"))
    {
        qWarning() << "Failed to prepare message insert:" << query.lastError().text();
        return false;
    }

    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 timestamp = msg.timestamp.isValid() ? msg.timestamp.toSecsSinceEpoch() : now;

    query.bindValue(":from", msg.fromNode);
    query.bindValue(":to", msg.toNode);
    query.bindValue(":channel", msg.channel);
    query.bindValue(":port_num", msg.portNum);
    query.bindValue(":text", msg.text);
    query.bindValue(":payload", msg.payload);
    query.bindValue(":timestamp", timestamp);
    query.bindValue(":read", msg.read ? 1 : 0);
    query.bindValue(":created_at", now);
    query.bindValue(":status", msg.status);
    query.bindValue(":packet_id", msg.packetId);

    if (!query.exec())
    {
        qWarning() << "Failed to save message:" << query.lastError().text();
        return false;
    }

    return true;
}

QList<Database::Message> Database::loadMessages(int limit, int offset)
{
    QList<Message> messages;
    if (!m_db.isOpen())
    {
        qWarning() << "Database not open in loadMessages";
        return messages;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM messages ORDER BY timestamp DESC LIMIT ? OFFSET ?");
    query.addBindValue(limit);
    query.addBindValue(offset);

    if (!query.exec())
    {
        qWarning() << "Failed to load messages:" << query.lastError().text();
        return messages;
    }

    while (query.next())
    {
        Message msg;
        msg.id = query.value("id").toLongLong();
        msg.fromNode = query.value("from_node").toUInt();
        msg.toNode = query.value("to_node").toUInt();
        msg.channel = query.value("channel").toString();
        msg.portNum = query.value("port_num").toInt();
        msg.text = query.value("text").toString();
        msg.payload = query.value("payload").toByteArray();
        msg.read = query.value("read").toBool();
        msg.status = query.value("status").toInt();
        msg.packetId = query.value("packet_id").toUInt();
        qint64 ts = query.value("timestamp").toLongLong();
        if (ts > 0)
        {
            msg.timestamp = QDateTime::fromSecsSinceEpoch(ts);
        }
        messages.append(msg);
    }

    qDebug() << "[Database] Loaded" << messages.size() << "total messages from db";
    return messages;
}

bool Database::saveTraceroute(const Traceroute &tr)
{
    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO traceroutes (from_node, to_node, route_to, route_back, snr_to, snr_back, timestamp, is_response)
        VALUES (:from_node, :to_node, :route_to, :route_back, :snr_to, :snr_back, :timestamp, :is_response)
    )");

    qint64 timestamp = tr.timestamp.isValid() ? tr.timestamp.toSecsSinceEpoch() : QDateTime::currentSecsSinceEpoch();

    query.bindValue(":from_node", tr.fromNode);
    query.bindValue(":to_node", tr.toNode);
    query.bindValue(":route_to", tr.routeTo.join(";"));
    query.bindValue(":route_back", tr.routeBack.join(";"));
    query.bindValue(":snr_to", tr.snrTo.join(";"));
    query.bindValue(":snr_back", tr.snrBack.join(";"));
    query.bindValue(":timestamp", timestamp);
    query.bindValue(":is_response", tr.isResponse ? 1 : 0);

    if (!query.exec())
    {
        qWarning() << "Failed to save traceroute:" << query.lastError().text();
        return false;
    }

    return true;
}

QList<Database::Traceroute> Database::loadTraceroutes(int limit, int offset)
{
    QList<Traceroute> traceroutes;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM traceroutes ORDER BY timestamp DESC LIMIT ? OFFSET ?");
    query.addBindValue(limit);
    query.addBindValue(offset);

    if (!query.exec())
    {
        qWarning() << "Failed to load traceroutes:" << query.lastError().text();
        return traceroutes;
    }

    while (query.next())
    {
        Traceroute tr;
        tr.id = query.value("id").toLongLong();
        tr.fromNode = query.value("from_node").toUInt();
        tr.toNode = query.value("to_node").toUInt();
        tr.routeTo = query.value("route_to").toString().split(";", Qt::SkipEmptyParts);
        tr.routeBack = query.value("route_back").toString().split(";", Qt::SkipEmptyParts);
        tr.snrTo = query.value("snr_to").toString().split(";", Qt::SkipEmptyParts);
        tr.snrBack = query.value("snr_back").toString().split(";", Qt::SkipEmptyParts);
        tr.isResponse = query.value("is_response").toBool();
        qint64 ts = query.value("timestamp").toLongLong();
        if (ts > 0)
        {
            tr.timestamp = QDateTime::fromSecsSinceEpoch(ts);
        }
        traceroutes.append(tr);
    }

    return traceroutes;
}

bool Database::deleteTraceroutes(int daysOld)
{
    QSqlQuery query(m_db);
    qint64 cutoffTime = QDateTime::currentSecsSinceEpoch() - (daysOld * 86400); // 86400 seconds per day
    query.prepare("DELETE FROM traceroutes WHERE timestamp < ?");
    query.addBindValue(cutoffTime);

    if (!query.exec())
    {
        qWarning() << "Failed to delete traceroutes:" << query.lastError().text();
        return false;
    }

    return true;
}

QList<Database::Message> Database::loadMessagesForNode(uint32_t nodeNum, int limit)
{
    QList<Message> messages;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM messages WHERE from_node = ? OR to_node = ? ORDER BY timestamp DESC LIMIT ?");
    query.addBindValue(nodeNum);
    query.addBindValue(nodeNum);
    query.addBindValue(limit);

    if (!query.exec())
    {
        return messages;
    }

    while (query.next())
    {
        Message msg;
        msg.id = query.value("id").toLongLong();
        msg.fromNode = query.value("from_node").toUInt();
        msg.toNode = query.value("to_node").toUInt();
        msg.channel = query.value("channel").toString();
        msg.portNum = query.value("port_num").toInt();
        msg.text = query.value("text").toString();
        msg.payload = query.value("payload").toByteArray();
        msg.read = query.value("read").toBool();
        qint64 ts = query.value("timestamp").toLongLong();
        if (ts > 0)
        {
            msg.timestamp = QDateTime::fromSecsSinceEpoch(ts);
        }
        messages.append(msg);
    }

    return messages;
}

bool Database::markMessageRead(qint64 messageId)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE messages SET read = 1 WHERE id = ?");
    query.addBindValue(messageId);
    return query.exec();
}

bool Database::updateMessageStatus(uint32_t packetId, int status)
{
    if (packetId == 0)
        return false;

    if (!m_db.isOpen())
    {
        qWarning() << "Database not open in updateMessageStatus";
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("UPDATE messages SET status = ? WHERE packet_id = ?");
    query.addBindValue(status);
    query.addBindValue(packetId);

    if (!query.exec())
    {
        qWarning() << "Failed to update message status:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

int Database::unreadMessageCount()
{
    QSqlQuery query(m_db);
    if (query.exec("SELECT COUNT(*) FROM messages WHERE read = 0") && query.next())
    {
        return query.value(0).toInt();
    }
    return 0;
}

bool Database::deleteMessagesWithNode(uint32_t nodeNum)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM messages WHERE from_node = ? OR to_node = ?");
    query.addBindValue(nodeNum);
    query.addBindValue(nodeNum);

    if (!query.exec())
    {
        qWarning() << "Failed to delete messages:" << query.lastError().text();
        return false;
    }

    qDebug() << "Deleted messages with node" << nodeNum;
    return true;
}

QList<ChatMessage> Database::getAllMessages()
{
    QList<ChatMessage> messages;
    QSqlQuery query(m_db);

    if (!query.exec("SELECT * FROM messages ORDER BY timestamp ASC"))
    {
        qWarning() << "Failed to load all messages:" << query.lastError().text();
        return messages;
    }

    while (query.next())
    {
        ChatMessage msg;
        msg.id = query.value("id").toLongLong();
        msg.fromNode = query.value("from_node").toUInt();
        msg.toNode = query.value("to_node").toUInt();
        msg.channelIndex = query.value("channel").toString().toInt();
        msg.text = query.value("text").toString();
        msg.read = query.value("read").toBool();
        msg.packetId = query.value("packet_id").toUInt();
        qint64 ts = query.value("timestamp").toLongLong();
        if (ts > 0)
        {
            msg.timestamp = QDateTime::fromSecsSinceEpoch(ts);
        }
        messages.append(msg);
    }

    return messages;
}

// Telemetry history operations

bool Database::saveTelemetryRecord(const TelemetryRecord &record)
{
    if (!m_db.isOpen() || record.nodeNum == 0)
        return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO telemetry_history (
            node_num, timestamp, temperature, humidity, pressure,
            battery_level, voltage, snr, rssi, channel_util, air_util_tx
        ) VALUES (
            :node_num, :timestamp, :temperature, :humidity, :pressure,
            :battery_level, :voltage, :snr, :rssi, :channel_util, :air_util_tx
        )
    )");

    qint64 timestamp = record.timestamp.isValid() ? record.timestamp.toSecsSinceEpoch() : QDateTime::currentSecsSinceEpoch();

    query.bindValue(":node_num", record.nodeNum);
    query.bindValue(":timestamp", timestamp);
    query.bindValue(":temperature", record.temperature);
    query.bindValue(":humidity", record.humidity);
    query.bindValue(":pressure", record.pressure);
    query.bindValue(":battery_level", record.batteryLevel);
    query.bindValue(":voltage", record.voltage);
    query.bindValue(":snr", record.snr);
    query.bindValue(":rssi", record.rssi);
    query.bindValue(":channel_util", record.channelUtil);
    query.bindValue(":air_util_tx", record.airUtilTx);

    if (!query.exec())
    {
        qWarning() << "Failed to save telemetry record:" << query.lastError().text();
        return false;
    }

    return true;
}

QList<Database::TelemetryRecord> Database::loadTelemetryHistory(uint32_t nodeNum, int hours)
{
    QList<TelemetryRecord> records;
    if (!m_db.isOpen())
        return records;

    QSqlQuery query(m_db);
    qint64 cutoff = QDateTime::currentSecsSinceEpoch() - (hours * 3600);

    query.prepare(R"(
        SELECT * FROM telemetry_history
        WHERE node_num = ? AND timestamp >= ?
        ORDER BY timestamp ASC
    )");
    query.addBindValue(nodeNum);
    query.addBindValue(cutoff);

    if (!query.exec())
    {
        qWarning() << "Failed to load telemetry history:" << query.lastError().text();
        return records;
    }

    while (query.next())
    {
        TelemetryRecord rec;
        rec.id = query.value("id").toLongLong();
        rec.nodeNum = query.value("node_num").toUInt();
        rec.temperature = query.value("temperature").toFloat();
        rec.humidity = query.value("humidity").toFloat();
        rec.pressure = query.value("pressure").toFloat();
        rec.batteryLevel = query.value("battery_level").toInt();
        rec.voltage = query.value("voltage").toFloat();
        rec.snr = query.value("snr").toFloat();
        rec.rssi = query.value("rssi").toInt();
        rec.channelUtil = query.value("channel_util").toFloat();
        rec.airUtilTx = query.value("air_util_tx").toFloat();
        qint64 ts = query.value("timestamp").toLongLong();
        if (ts > 0)
            rec.timestamp = QDateTime::fromSecsSinceEpoch(ts);
        records.append(rec);
    }

    return records;
}

QList<uint32_t> Database::getNodesWithTelemetry()
{
    QList<uint32_t> nodes;
    if (!m_db.isOpen())
        return nodes;

    QSqlQuery query(m_db);
    if (!query.exec("SELECT DISTINCT node_num FROM telemetry_history ORDER BY node_num"))
    {
        qWarning() << "Failed to get nodes with telemetry:" << query.lastError().text();
        return nodes;
    }

    while (query.next())
    {
        nodes.append(query.value(0).toUInt());
    }

    return nodes;
}

bool Database::deleteTelemetryHistory(int daysOld)
{
    if (!m_db.isOpen())
        return false;

    QSqlQuery query(m_db);
    qint64 cutoff = QDateTime::currentSecsSinceEpoch() - (daysOld * 86400);
    query.prepare("DELETE FROM telemetry_history WHERE timestamp < ?");
    query.addBindValue(cutoff);

    if (!query.exec())
    {
        qWarning() << "Failed to delete old telemetry history:" << query.lastError().text();
        return false;
    }

    return true;
}

// Position history operations

bool Database::savePosition(const PositionRecord &record)
{
    if (!m_db.isOpen() || record.nodeNum == 0)
        return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO position_history (
            node_num, latitude, longitude, altitude, timestamp
        ) VALUES (
            :node_num, :latitude, :longitude, :altitude, :timestamp
        )
    )");

    qint64 timestamp = record.timestamp.isValid() ? record.timestamp.toSecsSinceEpoch() : QDateTime::currentSecsSinceEpoch();

    query.bindValue(":node_num", record.nodeNum);
    query.bindValue(":latitude", record.latitude);
    query.bindValue(":longitude", record.longitude);
    query.bindValue(":altitude", record.altitude);
    query.bindValue(":timestamp", timestamp);

    if (!query.exec())
    {
        qWarning() << "Failed to save position record:" << query.lastError().text();
        return false;
    }

    return true;
}

Database::PositionRecord Database::loadPositionAt(uint32_t nodeNum, qint64 timestamp)
{
    PositionRecord rec;
    rec.nodeNum = nodeNum;

    if (!m_db.isOpen())
        return rec;

    QSqlQuery query(m_db);
    // Find the record closest in time to the requested timestamp
    query.prepare(R"(
        SELECT * FROM position_history
        WHERE node_num = ?
        ORDER BY ABS(timestamp - ?) ASC
        LIMIT 1
    )");
    query.addBindValue(nodeNum);
    query.addBindValue(timestamp);

    if (query.exec() && query.next())
    {
        rec.latitude = query.value("latitude").toDouble();
        rec.longitude = query.value("longitude").toDouble();
        rec.altitude = query.value("altitude").toInt();
        qint64 ts = query.value("timestamp").toLongLong();
        if (ts > 0)
            rec.timestamp = QDateTime::fromSecsSinceEpoch(ts);
    }

    return rec;
}

bool Database::savePacket(const PacketRecord &record)
{
    if (!m_db.isOpen())
        return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO packets (
            timestamp, packet_type, from_node, to_node, port_num, channel,
            type_name, raw_data, fields_json
        ) VALUES (
            :timestamp, :packet_type, :from_node, :to_node, :port_num, :channel,
            :type_name, :raw_data, :fields_json
        )
    )");

    query.bindValue(":timestamp", record.timestamp);
    query.bindValue(":packet_type", record.packetType);
    query.bindValue(":from_node", record.fromNode);
    query.bindValue(":to_node", record.toNode);
    query.bindValue(":port_num", record.portNum);
    query.bindValue(":channel", record.channel);
    query.bindValue(":type_name", record.typeName);
    query.bindValue(":raw_data", record.rawData);
    query.bindValue(":fields_json", record.fieldsJson);

    if (!query.exec())
    {
        qWarning() << "Failed to save packet:" << query.lastError().text();
        return false;
    }

    return true;
}

QList<Database::PacketRecord> Database::loadPackets(int limit, int offset)
{
    QList<PacketRecord> packets;
    if (!m_db.isOpen())
        return packets;

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM packets ORDER BY timestamp DESC LIMIT ? OFFSET ?");
    query.addBindValue(limit);
    query.addBindValue(offset);

    if (!query.exec())
    {
        qWarning() << "Failed to load packets:" << query.lastError().text();
        return packets;
    }

    while (query.next())
    {
        PacketRecord rec;
        rec.id = query.value("id").toLongLong();
        rec.timestamp = query.value("timestamp").toLongLong();
        rec.packetType = query.value("packet_type").toInt();
        rec.fromNode = query.value("from_node").toUInt();
        rec.toNode = query.value("to_node").toUInt();
        rec.portNum = query.value("port_num").toInt();
        rec.channel = query.value("channel").toInt();
        rec.typeName = query.value("type_name").toString();
        rec.rawData = query.value("raw_data").toByteArray();
        rec.fieldsJson = query.value("fields_json").toString();
        packets.append(rec);
    }

    return packets;
}

bool Database::deleteOldPackets(int daysOld)
{
    if (!m_db.isOpen())
        return false;

    QSqlQuery query(m_db);
    qint64 cutoffTime = QDateTime::currentMSecsSinceEpoch() - (static_cast<qint64>(daysOld) * 86400 * 1000);
    query.prepare("DELETE FROM packets WHERE timestamp < ?");
    query.addBindValue(cutoffTime);

    if (!query.exec())
    {
        qWarning() << "Failed to delete old packets:" << query.lastError().text();
        return false;
    }

    int deleted = query.numRowsAffected();
    if (deleted > 0)
        qDebug() << "Deleted" << deleted << "old packets";

    return true;
}
