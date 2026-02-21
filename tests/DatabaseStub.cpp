// Minimal stub so test_nodemanager can link without pulling in the real
// Database.cpp (which depends on MessagesWidget / QWidget).
// NodeManager guards every Database call with "if (m_database)", so none of
// these stubs are ever executed during tests — the linker just needs them.

#include "Database.h"

Database::Database(QObject *parent) : QObject(parent) {}
Database::~Database() {}

bool Database::open(const QString &) { return false; }
void Database::close() {}
bool Database::isOpen() const { return false; }

bool Database::saveNode(const NodeInfo &)       { return false; }
bool Database::saveNodes(const QList<NodeInfo> &) { return false; }
NodeInfo Database::loadNode(uint32_t)            { return {}; }
QList<NodeInfo> Database::loadAllNodes()         { return {}; }
bool Database::deleteNode(uint32_t)              { return false; }
int  Database::nodeCount()                       { return 0; }

bool Database::saveMessage(const Message &)              { return false; }
QList<Database::Message> Database::loadMessages(int, int){ return {}; }

bool Database::saveTraceroute(const Traceroute &)                { return false; }
QList<Database::Traceroute> Database::loadTraceroutes(int, int)  { return {}; }
bool Database::deleteTraceroutes(int)                            { return false; }

bool Database::saveTelemetryRecord(const TelemetryRecord &)                    { return false; }
QList<Database::TelemetryRecord> Database::loadTelemetryHistory(uint32_t, int) { return {}; }
QList<uint32_t> Database::getNodesWithTelemetry()                              { return {}; }
bool Database::deleteTelemetryHistory(int)                                     { return false; }

bool Database::savePosition(const PositionRecord &)                            { return false; }
Database::PositionRecord Database::loadPositionAt(uint32_t, qint64)            { return {}; }

bool Database::savePacket(const PacketRecord &)                  { return false; }
QList<Database::PacketRecord> Database::loadPackets(int, int)    { return {}; }
bool Database::deleteOldPackets(int)                             { return false; }

bool Database::saveNeighborInfo(uint32_t, const QList<NeighborRecord> &)         { return false; }
QMap<uint32_t, QList<Database::NeighborRecord>> Database::loadAllNeighborInfo() { return {}; }
bool Database::deleteOldNeighborInfo(int)                                        { return false; }

QList<Database::Message> Database::loadMessagesForNode(uint32_t, int) { return {}; }
bool Database::markMessageRead(qint64)                                 { return false; }
bool Database::updateMessageStatus(uint32_t, int)                      { return false; }
int  Database::unreadMessageCount()                                    { return 0; }
bool Database::deleteMessagesWithNode(uint32_t)                        { return false; }

// getAllMessages() returns ChatMessage which is defined in MessagesWidget.h.
// We forward-declare a minimal ChatMessage here so we can return an empty list.
struct ChatMessage {};
QList<ChatMessage> Database::getAllMessages() { return {}; }

bool Database::createTables()                        { return false; }
bool Database::migrateSchema(int, int)               { return false; }
int  Database::getSchemaVersion()                    { return 0; }
void Database::setSchemaVersion(int)                 {}
bool Database::prepareStatements()                   { return false; }
void Database::cleanupStatements()                   {}
