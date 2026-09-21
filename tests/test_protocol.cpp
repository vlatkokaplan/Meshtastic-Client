#include <QtTest/QtTest>
#include <QSignalSpy>
#include "MeshtasticProtocol.h"
#include "DeviceConfig.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"

#include <openssl/evp.h>

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

    void xor_hash_matches_firmware()
    {
        // Firmware xorHash() is a plain XOR fold over the bytes
        QCOMPARE(MeshtasticProtocol::xorHash(QByteArray()), static_cast<uint8_t>(0));
        QCOMPARE(MeshtasticProtocol::xorHash(QByteArray("LongFast")), static_cast<uint8_t>(0x0A));
        QCOMPARE(MeshtasticProtocol::xorHash(QByteArray::fromHex("d4f1bb3a20290759f0bcffabcf4e6901")),
                 static_cast<uint8_t>(0x02));
    }

    void default_longfast_channel_hashes_to_8()
    {
        // The stock Meshtastic primary channel - unnamed, modem preset LONG_FAST,
        // psk "AQ==" (the single byte 0x01 => the unmodified default PSK) - is
        // well known to hash to 8. This is the value that arrives in the
        // `channel` field of an encrypted MeshPacket.
        DeviceConfig cfg;

        DeviceConfig::LoRaConfig lora;
        lora.modemPreset = 0;  // LONG_FAST
        cfg.setLoRaConfig(lora);

        DeviceConfig::ChannelConfig ch;
        ch.index = 0;
        ch.role = 1;  // primary
        ch.name = QString();
        ch.psk = QByteArray(1, 0x01);
        cfg.setChannel(0, ch);

        MeshtasticProtocol proto;
        proto.setDeviceConfig(&cfg);

        QCOMPARE(proto.channelHashFor(0), 8);
    }

    void channel_hash_is_minus_one_for_disabled_channel()
    {
        DeviceConfig cfg;
        DeviceConfig::ChannelConfig ch;
        ch.index = 1;
        ch.role = 0;  // disabled
        ch.name = QString("Secondary");
        ch.psk = QByteArray(1, 0x01);
        cfg.setChannel(1, ch);

        MeshtasticProtocol proto;
        proto.setDeviceConfig(&cfg);

        QCOMPARE(proto.channelHashFor(1), -1);
    }

    void named_channel_hash_differs_from_default()
    {
        DeviceConfig cfg;
        DeviceConfig::ChannelConfig ch;
        ch.index = 0;
        ch.role = 1;
        ch.name = QString("admin");
        ch.psk = QByteArray(1, 0x01);
        cfg.setChannel(0, ch);

        MeshtasticProtocol proto;
        proto.setDeviceConfig(&cfg);

        // xorHash("admin") ^ xorHash(defaultpsk)
        uint8_t expected = MeshtasticProtocol::xorHash(QByteArray("admin")) ^ 0x02;
        QCOMPARE(proto.channelHashFor(0), static_cast<int>(expected));
    }

    // ---- decrypting a message on a configured channel ---------------------
    // The app decrypts what the device hands over encrypted. A message on a
    // channel we hold the key for must come out with the channel it belongs to,
    // because that is what tells the UI it is ours rather than a stranger's
    // traffic recovered by sweeping the default keys.

    void encrypted_text_on_a_configured_channel_is_resolved()
    {
        DeviceConfig cfg;
        DeviceConfig::LoRaConfig lora;
        lora.modemPreset = 0;                    // LONG_FAST
        cfg.setLoRaConfig(lora);

        DeviceConfig::ChannelConfig ch;
        ch.index = 0;
        ch.role = 1;                             // primary
        ch.psk = QByteArray(1, 0x01);            // the default key, "AQ=="
        cfg.setChannel(0, ch);

        MeshtasticProtocol proto;
        proto.setDeviceConfig(&cfg);

        const int hash = proto.channelHashFor(0);
        QCOMPARE(hash, 8);                       // the well-known LongFast hash

        // Build the Data payload the radio would have encrypted
        meshtastic::Data data;
        data.set_portnum(meshtastic::PortNum::TEXT_MESSAGE_APP);
        data.set_payload("hello mesh");
        std::string plain;
        QVERIFY(data.SerializeToString(&plain));

        const uint32_t packetId = 0x11223344;
        const uint32_t fromNode = 0xb29c7344;

        // Same nonce layout as the firmware: packetId as u64 LE, then fromNode
        unsigned char nonce[16] = {0};
        for (int i = 0; i < 4; ++i)
            nonce[i] = (packetId >> (8 * i)) & 0xFF;
        for (int i = 0; i < 4; ++i)
            nonce[8 + i] = (fromNode >> (8 * i)) & 0xFF;

        const QByteArray key = QByteArray::fromHex("d4f1bb3a20290759f0bcffabcf4e6901");
        QByteArray cipher(static_cast<int>(plain.size()), 0);
        int outLen = 0;
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        QVERIFY(ctx);
        QVERIFY(EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr,
                                   reinterpret_cast<const unsigned char *>(key.constData()),
                                   nonce) == 1);
        QVERIFY(EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(cipher.data()), &outLen,
                                  reinterpret_cast<const unsigned char *>(plain.data()),
                                  static_cast<int>(plain.size())) == 1);
        EVP_CIPHER_CTX_free(ctx);
        cipher.resize(outLen);

        meshtastic::FromRadio fr;
        auto *pkt = fr.mutable_packet();
        pkt->set_id(packetId);
        pkt->set_from(fromNode);
        pkt->set_to(0xFFFFFFFF);                 // broadcast
        pkt->set_channel(hash);                  // the wire carries the hash
        pkt->set_encrypted(cipher.constData(), cipher.size());

        std::string wire;
        QVERIFY(fr.SerializeToString(&wire));

        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);
        proto.processIncomingData(makeFrame(wire));

        QCOMPARE(spy.count(), 1);
        auto decoded = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();

        QCOMPARE(decoded.fields["text"].toString(), QString("hello mesh"));
        QVERIFY2(decoded.fields.contains("resolvedChannel"),
                 "a message decrypted with a configured channel's key must say which channel");
        QCOMPARE(decoded.fields["resolvedChannel"].toInt(), 0);
        // channelIndex must be the real index, not the hash that arrived
        QCOMPARE(decoded.channelIndex, 0);
        QVERIFY(decoded.channelIndex != hash);
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
