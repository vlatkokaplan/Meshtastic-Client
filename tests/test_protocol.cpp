#include <QtTest/QtTest>
#include <QSignalSpy>
#include "MeshtasticProtocol.h"
#include "meshtastic/mesh.pb.h"

// Build a valid framed FromRadio packet from a serialized protobuf
static QByteArray makeFrame(const std::string &payload)
{
    QByteArray f;
    uint16_t len = static_cast<uint16_t>(payload.size());
    f.append(static_cast<char>(MeshtasticProtocol::SYNC_BYTE_1));
    f.append(static_cast<char>(MeshtasticProtocol::SYNC_BYTE_2));
    f.append(static_cast<char>((len >> 8) & 0xFF));
    f.append(static_cast<char>(len & 0xFF));
    f.append(QByteArray::fromStdString(payload));
    return f;
}

static QByteArray myInfoFrame(uint32_t nodeNum)
{
    meshtastic::FromRadio fr;
    fr.mutable_my_info()->set_my_node_num(nodeNum);
    std::string s;
    fr.SerializeToString(&s);
    return makeFrame(s);
}

static QByteArray configCompleteFrame(uint32_t id)
{
    meshtastic::FromRadio fr;
    fr.set_config_complete_id(id);
    std::string s;
    fr.SerializeToString(&s);
    return makeFrame(s);
}

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    void parses_MyInfo_packet()
    {
        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);

        proto.processIncomingData(myInfoFrame(0xDEADBEEF));

        QCOMPARE(spy.count(), 1);
        auto pkt = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();
        QCOMPARE(pkt.type, MeshtasticProtocol::PacketType::MyInfo);
        QCOMPARE(pkt.fields["myNodeNum"].toUInt(), 0xDEADBEEFu);
    }

    void parses_ConfigCompleteId_packet()
    {
        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);

        proto.processIncomingData(configCompleteFrame(42));

        QCOMPARE(spy.count(), 1);
        auto pkt = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();
        QCOMPARE(pkt.type, MeshtasticProtocol::PacketType::ConfigCompleteId);
        QCOMPARE(pkt.fields["configId"].toUInt(), 42u);
    }

    void parses_two_frames_in_one_chunk()
    {
        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);

        QByteArray chunk = myInfoFrame(0x11111111) + configCompleteFrame(99);
        proto.processIncomingData(chunk);

        QCOMPARE(spy.count(), 2);
    }

    void handles_split_frame()
    {
        // Feed a frame byte-by-byte — should emit exactly one signal at the end
        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);

        QByteArray frame = myInfoFrame(0xABCD1234);
        for (int i = 0; i < frame.size() - 1; ++i) {
            proto.processIncomingData(frame.mid(i, 1));
            QCOMPARE(spy.count(), 0);
        }
        proto.processIncomingData(frame.mid(frame.size() - 1, 1));
        QCOMPARE(spy.count(), 1);
    }

    void resetParser_discards_partial_frame()
    {
        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);

        QByteArray frame = myInfoFrame(0x12345678);
        // Feed half the frame, then reset, then feed a fresh complete frame
        proto.processIncomingData(frame.mid(0, frame.size() / 2));
        QCOMPARE(spy.count(), 0);

        proto.resetParser();

        // Fresh frame — must parse cleanly even though partial junk was fed before
        proto.processIncomingData(myInfoFrame(0x99999999));
        QCOMPARE(spy.count(), 1);
        auto pkt = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();
        QCOMPARE(pkt.fields["myNodeNum"].toUInt(), 0x99999999u);
    }

    void ignores_garbage_bytes_before_sync()
    {
        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);

        QByteArray junk = QByteArray("\x00\xFF\x12\x34\xAB", 5);
        QByteArray data = junk + myInfoFrame(0xCAFEBABE);
        proto.processIncomingData(data);

        QCOMPARE(spy.count(), 1);
        auto pkt = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();
        QCOMPARE(pkt.fields["myNodeNum"].toUInt(), 0xCAFEBABEu);
    }
};

QTEST_MAIN(TestProtocol)
#include "test_protocol.moc"
