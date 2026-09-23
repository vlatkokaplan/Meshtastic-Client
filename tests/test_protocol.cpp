#include <QtTest/QtTest>
#include <QSignalSpy>
#include "MeshtasticProtocol.h"
#include "DeviceConfig.h"
// moc cannot parse the [[deprecated]] enum values in generated protobuf headers
#ifndef Q_MOC_RUN
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/telemetry.pb.h"
#endif

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

// Pull the Data.payload (the AdminMessage bytes) out of a framed ToRadio
static QByteArray adminPayloadOf(const QByteArray &frame)
{
    meshtastic::ToRadio tr;
    if (frame.size() < 4 || !tr.ParseFromArray(frame.constData() + 4, frame.size() - 4))
        return QByteArray();
    return QByteArray::fromStdString(tr.packet().decoded().payload());
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

    void encrypted_text_on_an_unknown_channel_is_not_resolved()
    {
        // Same well-known key, but arriving under a channel hash none of our
        // channels produce. It still decrypts - the key is public - but it is
        // someone else's channel, and the UI has to be able to tell, because
        // that is what decides whether it can be replied to.
        DeviceConfig cfg;
        DeviceConfig::LoRaConfig lora;
        lora.modemPreset = 0;
        cfg.setLoRaConfig(lora);

        DeviceConfig::ChannelConfig ch;
        ch.index = 0;
        ch.role = 1;
        ch.name = "Srbija";              // hashes to something other than 16
        ch.psk = QByteArray(1, 0x01);
        cfg.setChannel(0, ch);

        MeshtasticProtocol proto;
        proto.setDeviceConfig(&cfg);

        const int foreignHash = 16;
        QVERIFY(proto.channelHashFor(0) != foreignHash);

        meshtastic::Data data;
        data.set_portnum(meshtastic::PortNum::TEXT_MESSAGE_APP);
        data.set_payload("not my channel");
        std::string plain;
        QVERIFY(data.SerializeToString(&plain));

        const uint32_t packetId = 0x55667788;
        const uint32_t fromNode = 0x0a0b0c0d;

        unsigned char nonce[16] = {0};
        for (int i = 0; i < 4; ++i) nonce[i] = (packetId >> (8 * i)) & 0xFF;
        for (int i = 0; i < 4; ++i) nonce[8 + i] = (fromNode >> (8 * i)) & 0xFF;

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
        pkt->set_to(0xFFFFFFFF);
        pkt->set_channel(foreignHash);
        pkt->set_encrypted(cipher.constData(), cipher.size());

        std::string wire;
        QVERIFY(fr.SerializeToString(&wire));

        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);
        proto.processIncomingData(makeFrame(wire));

        QCOMPARE(spy.count(), 1);
        auto decoded = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();

        // Readable...
        QCOMPARE(decoded.fields["text"].toString(), QString("not my channel"));
        QVERIFY(decoded.fields.contains("decrypted"));
        // ...but not ours: no channel resolved, and the hash passes through
        QVERIFY2(!decoded.fields.contains("resolvedChannel"),
                 "a channel we hold no key for must not be reported as one of ours");
        QCOMPARE(decoded.channelIndex, foreignHash);
    }

    void real_captured_tapback_is_recognised()
    {
        // A genuine reaction captured off the air: "\u2615" sent as a tapback to
        // an earlier message. Kept verbatim because the wire encoding of the
        // emoji field is what decides whether a reaction is recognised at all,
        // and a hand-built packet would only prove our own encoder agrees with
        // our own decoder.
        const QByteArray frame = QByteArray::fromHex(
            "12440DBC8A60AE15FFFFFFFF221308011203E298953DA8A402724501000000"
            "480035C0F919C03D5915B26A4500002C41480160C8FFFFFFFFFFFFFFFF0178"
            "039801EE01A80101");

        meshtastic::FromRadio fr;
        QVERIFY(fr.ParseFromArray(frame.constData(), frame.size()));
        QVERIFY(fr.has_packet());

        const auto &data = fr.packet().decoded();
        QCOMPARE(static_cast<int>(data.portnum()), 1);          // TEXT_MESSAGE_APP
        QCOMPARE(QString::fromStdString(data.payload()), QString("\u2615"));

        // The two fields that make this a tapback rather than a message
        QCOMPARE(static_cast<uint32_t>(data.reply_id()), 1912775848u);
        QVERIFY2(data.emoji() != 0,
                 "emoji must decode non-zero, or a reaction is indistinguishable "
                 "from a message that happens to contain one");
    }

    // Hash for an unnamed channel with the default key, given the LoRa
    // settings. Expected values are xorHash(name) ^ xorHash(defaultpsk),
    // computed outside this codebase with firmware's preset names.
    static int unnamedChannelHash(int channelIndex, int modemPreset, bool usePreset)
    {
        DeviceConfig cfg;
        DeviceConfig::LoRaConfig lora;
        lora.modemPreset = modemPreset;
        lora.usePreset = usePreset;
        cfg.setLoRaConfig(lora);

        DeviceConfig::ChannelConfig ch;
        ch.index = channelIndex;
        ch.role = channelIndex == 0 ? 1 : 2;
        ch.psk = QByteArray(1, 0x01);
        cfg.setChannel(channelIndex, ch);

        MeshtasticProtocol proto;
        proto.setDeviceConfig(&cfg);
        return proto.channelHashFor(channelIndex);
    }

    void newer_presets_hash_with_firmware_names()
    {
        QCOMPARE(unnamedChannelHash(0, 9, true), 118);   // LONG_TURBO -> "LongTurbo"
        QCOMPARE(unnamedChannelHash(0, 16, true), 97);   // MEDIUM_TURBO -> "MediumTurbo"
    }

    void custom_modem_settings_hash_as_Custom()
    {
        // use_preset off: firmware names the channel "Custom" whatever the preset
        QCOMPARE(unnamedChannelHash(0, 0, false), 49);
    }

    void unnamed_secondary_channel_takes_the_preset_name()
    {
        // Channels::getName applies to every index, not only the primary
        QCOMPARE(unnamedChannelHash(1, 0, true), 8);
    }

    void reaction_sets_the_emoji_field()
    {
        // Data.emoji = 8, fixed32: tag 0x45 + 01 00 00 00
        MeshtasticProtocol proto;
        const QByteArray reaction = proto.createTextMessagePacket(
            QString::fromUtf8("\xF0\x9F\x91\x8D"), 0xFFFFFFFF, 1, 0, 0x1234, nullptr, true);
        QVERIFY2(reaction.contains(QByteArray::fromHex("4501000000")), reaction.toHex().constData());

        const QByteArray reply = proto.createTextMessagePacket("ok", 0xFFFFFFFF, 1, 0, 0x1234);
        QVERIFY(!reply.contains(QByteArray::fromHex("4501000000")));
    }

    void lora_save_keeps_fields_the_ui_does_not_edit()
    {
        // What the device reported, including settings this client has no UI for
        meshtastic::Config_LoRaConfig device;
        device.set_use_preset(true);
        device.set_region(meshtastic::Config_LoRaConfig::EU_868);
        device.set_hop_limit(3);
        device.set_ignore_mqtt(true);
        device.set_config_ok_to_mqtt(true);
        device.set_override_frequency(869.525f);
        device.add_ignore_incoming(0xDEADBEEF);

        QVariantMap edited;
        edited["raw"] = QByteArray::fromStdString(device.SerializeAsString());
        edited["usePreset"] = true;
        edited["region"] = 3;
        edited["hopLimit"] = 5;  // the one change

        MeshtasticProtocol proto;
        meshtastic::AdminMessage admin;
        const QByteArray payload = adminPayloadOf(proto.createLoRaConfigPacket(1, 1, edited));
        QVERIFY(admin.ParseFromArray(payload.constData(), payload.size()));
        const auto &sent = admin.set_config().lora();

        QCOMPARE(sent.hop_limit(), 5u);
        QVERIFY(sent.ignore_mqtt());
        QVERIFY(sent.config_ok_to_mqtt());
        QCOMPARE(sent.override_frequency(), 869.525f);
        QCOMPARE(sent.ignore_incoming_size(), 1);
    }

    void enum_options_come_from_upstream_protos()
    {
        // Labels come from (meshtastic.enum_value_metadata); if the extension
        // were not linked these would fall back to the raw enum names.
        QCOMPARE(DeviceConfig::modemPresetName(9), QString("Long Range - Turbo"));
        QCOMPARE(DeviceConfig::deviceRoleName(12), QString("Client Base"));
        QCOMPARE(DeviceConfig::regionName(3), QString("EU_868"));

        bool routerClientDeprecated = false;
        for (const auto &o : DeviceConfig::deviceRoleOptions())
            if (o.value == 3)
                routerClientDeprecated = o.deprecated;
        QVERIFY(routerClientDeprecated);
    }

    void packet_ids_are_unique_and_nonzero()
    {
        // Time-based ids collided for packets built in the same millisecond
        MeshtasticProtocol proto;
        QSet<uint32_t> seen;
        for (int i = 0; i < 2000; ++i) {
            const uint32_t id = proto.nextPacketId();
            QVERIFY(id != 0);
            seen.insert(id);
        }
        QCOMPARE(seen.size(), 2000);
    }

    void traceroute_last_hop_snr_is_not_counted_twice()
    {
        // Current firmware: snr_back already has route_back + 1 entries
        meshtastic::RouteDiscovery rd;
        rd.add_route_back(0xAAAA0001);
        rd.add_snr_back(12);   // 3.0 dB at the relay
        rd.add_snr_back(36);   // 9.0 dB at us, appended by our firmware

        meshtastic::FromRadio fr;
        auto *p = fr.mutable_packet();
        p->set_from(0xAAAA0003);
        p->set_to(0x1);
        p->set_rx_snr(9.0f);
        p->mutable_decoded()->set_portnum(meshtastic::TRACEROUTE_APP);
        p->mutable_decoded()->set_request_id(42);
        p->mutable_decoded()->set_payload(rd.SerializeAsString());
        std::string s;
        fr.SerializeToString(&s);

        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);
        proto.processIncomingData(makeFrame(s));
        QCOMPARE(spy.count(), 1);
        auto pkt = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();
        const QVariantList snrBack = pkt.fields["snrBack"].toList();
        QCOMPARE(snrBack.size(), 2);
        QCOMPARE(snrBack[1].toDouble(), 9.0);
    }

    void traceroute_from_old_firmware_gets_last_hop_from_rx_snr()
    {
        // Older firmware left the final hop out; rx_snr fills it
        meshtastic::RouteDiscovery rd;
        rd.add_route_back(0xAAAA0001);
        rd.add_snr_back(12);

        meshtastic::FromRadio fr;
        auto *p = fr.mutable_packet();
        p->set_from(0xAAAA0003);
        p->set_to(0x1);
        p->set_rx_snr(9.0f);
        p->mutable_decoded()->set_portnum(meshtastic::TRACEROUTE_APP);
        p->mutable_decoded()->set_request_id(42);
        p->mutable_decoded()->set_payload(rd.SerializeAsString());
        std::string s;
        fr.SerializeToString(&s);

        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);
        proto.processIncomingData(makeFrame(s));
        auto pkt = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();
        const QVariantList snrBack = pkt.fields["snrBack"].toList();
        QCOMPARE(snrBack.size(), 2);
        QCOMPARE(snrBack[1].toDouble(), 9.0);
    }

    void pki_direct_message_is_not_brute_forced()
    {
        meshtastic::FromRadio fr;
        auto *p = fr.mutable_packet();
        p->set_from(0x1234);
        p->set_to(0x1);
        p->set_id(7);
        p->set_channel(0);
        p->set_pki_encrypted(true);
        p->set_encrypted(std::string(40, '\x5a'));
        std::string s;
        fr.SerializeToString(&s);

        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);
        proto.processIncomingData(makeFrame(s));
        auto pkt = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();
        QVERIFY(pkt.fields["pkiEncrypted"].toBool());
        QVERIFY(!pkt.fields.contains("decrypted"));
    }

    void client_notification_is_decoded()
    {
        meshtastic::FromRadio fr;
        fr.mutable_clientnotification()->set_message("Duty cycle limit reached");
        fr.mutable_clientnotification()->set_level(meshtastic::LogRecord::WARNING);
        std::string s;
        fr.SerializeToString(&s);

        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);
        proto.processIncomingData(makeFrame(s));
        auto pkt = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();
        QCOMPARE(pkt.type, MeshtasticProtocol::PacketType::ClientNotification);
        QCOMPARE(pkt.fields["message"].toString(), QString("Duty cycle limit reached"));
    }

    void security_save_keeps_the_keys()
    {
        meshtastic::Config_SecurityConfig device;
        device.set_public_key(std::string(32, '\x11'));
        device.set_private_key(std::string(32, '\x22'));
        device.add_admin_key(std::string(32, '\x33'));
        device.set_serial_enabled(true);

        QVariantMap edited;
        edited["raw"] = QByteArray::fromStdString(device.SerializeAsString());
        edited["serialEnabled"] = true;
        edited["debugLogApiEnabled"] = true;  // the one change

        MeshtasticProtocol proto;
        meshtastic::AdminMessage admin;
        const QByteArray payload = adminPayloadOf(proto.createSecurityConfigPacket(1, 1, edited));
        QVERIFY(admin.ParseFromArray(payload.constData(), payload.size()));
        const auto &sent = admin.set_config().security();
        QVERIFY(sent.debug_log_api_enabled());
        QCOMPARE(sent.private_key(), device.private_key());
        QCOMPARE(sent.public_key(), device.public_key());
        QCOMPARE(sent.admin_key_size(), 1);
    }

    void security_save_refuses_without_a_base()
    {
        MeshtasticProtocol proto;
        QVariantMap edited;
        edited["serialEnabled"] = false;
        QVERIFY(proto.createSecurityConfigPacket(1, 1, edited).isEmpty());
    }

    static MeshtasticProtocol::DecodedPacket decodeTelemetryPacket(const meshtastic::Telemetry &t)
    {
        meshtastic::FromRadio fr;
        auto *p = fr.mutable_packet();
        p->set_from(0x42);
        p->set_to(0xFFFFFFFF);
        p->mutable_decoded()->set_portnum(meshtastic::TELEMETRY_APP);
        p->mutable_decoded()->set_payload(t.SerializeAsString());
        std::string s;
        fr.SerializeToString(&s);

        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);
        proto.processIncomingData(makeFrame(s));
        return spy[0][0].value<MeshtasticProtocol::DecodedPacket>();
    }

    void telemetry_keeps_real_zero_readings()
    {
        meshtastic::Telemetry t;
        t.mutable_environment_metrics()->set_temperature(0.0f);       // 0 °C is a reading
        t.mutable_environment_metrics()->set_relative_humidity(55.0f);
        auto pkt = decodeTelemetryPacket(t);

        QCOMPARE(pkt.fields["telemetryType"].toString(), QString("environment"));
        QVERIFY(pkt.fields.contains("temperature"));
        QCOMPARE(pkt.fields["temperature"].toFloat(), 0.0f);
        // Unset fields stay absent rather than reading as 0
        QVERIFY(!pkt.fields.contains("barometricPressure"));
    }

    void telemetry_decodes_newer_variants()
    {
        meshtastic::Telemetry aq;
        aq.mutable_air_quality_metrics()->set_pm25_standard(12);
        aq.mutable_air_quality_metrics()->set_co2(640);
        auto pkt = decodeTelemetryPacket(aq);
        QCOMPARE(pkt.fields["telemetryType"].toString(), QString("airQuality"));
        QCOMPARE(pkt.fields["pm25Standard"].toUInt(), 12u);
        QCOMPARE(pkt.fields["co2"].toUInt(), 640u);

        meshtastic::Telemetry ls;
        ls.mutable_local_stats()->set_num_online_nodes(37);
        ls.mutable_local_stats()->set_noise_floor(-110);
        pkt = decodeTelemetryPacket(ls);
        QCOMPARE(pkt.fields["telemetryType"].toString(), QString("localStats"));
        QCOMPARE(pkt.fields["numOnlineNodes"].toUInt(), 37u);
        QCOMPARE(pkt.fields["noiseFloor"].toInt(), -110);
    }

    void startup_nodeinfo_carries_hops_mqtt_and_metrics()
    {
        meshtastic::FromRadio fr;
        auto *ni = fr.mutable_node_info();
        ni->set_num(0x77);
        ni->set_hops_away(2);
        ni->set_via_mqtt(true);
        ni->mutable_device_metrics()->set_battery_level(0);  // flat, not unknown
        ni->mutable_device_metrics()->set_voltage(3.3f);
        std::string s;
        fr.SerializeToString(&s);

        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);
        proto.processIncomingData(makeFrame(s));
        auto pkt = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();
        QCOMPARE(pkt.fields["hopsAway"].toInt(), 2);
        QVERIFY(pkt.fields["viaMqtt"].toBool());
        const QVariantMap metrics = pkt.fields["deviceMetrics"].toMap();
        QVERIFY(metrics.contains("batteryLevel"));
        QCOMPARE(metrics["batteryLevel"].toInt(), 0);
    }

    void factory_reset_uses_fields_99_and_94()
    {
        // factory_reset_config = 99, varint: tag 0x98 0x06
        // factory_reset_device = 94, varint: tag 0xf0 0x05
        MeshtasticProtocol proto;
        QCOMPARE(adminPayloadOf(proto.createFactoryResetPacket(1, 1, false)).toHex(), QByteArray("980601"));
        QCOMPARE(adminPayloadOf(proto.createFactoryResetPacket(1, 1, true)).toHex(), QByteArray("f00501"));
    }

    void custom_modem_settings_are_sent()
    {
        QVariantMap edited;
        edited["usePreset"] = false;
        edited["bandwidth"] = 62;     // 62.5 kHz
        edited["spreadFactor"] = 8;
        edited["codingRate"] = 6;

        MeshtasticProtocol proto;
        meshtastic::AdminMessage admin;
        const QByteArray payload = adminPayloadOf(proto.createLoRaConfigPacket(1, 1, edited));
        QVERIFY(admin.ParseFromArray(payload.constData(), payload.size()));
        const auto &lora = admin.set_config().lora();
        QVERIFY(!lora.use_preset());
        QCOMPARE(lora.bandwidth(), 62u);
        QCOMPARE(lora.spread_factor(), 8u);
        QCOMPARE(lora.coding_rate(), 6u);
    }

    void preset_params_match_firmware()
    {
        int bw = 0, sf = 0, cr = 0;
        DeviceConfig::presetModemParams(0, false, bw, sf, cr);   // LONG_FAST
        QCOMPARE(bw, 250); QCOMPARE(sf, 11); QCOMPARE(cr, 5);
        DeviceConfig::presetModemParams(0, true, bw, sf, cr);    // LONG_FAST on 2.4 GHz
        QCOMPARE(bw, 800);
        DeviceConfig::presetModemParams(12, false, bw, sf, cr);  // NARROW_FAST
        QCOMPARE(bw, 62); QCOMPARE(sf, 7); QCOMPARE(cr, 6);
    }

    // ---- Wire format against upstream meshtastic/protobufs -------------
    // Expected bytes are hand-encoded from the upstream field numbers and
    // wire types, not produced by our own generated code, so an edit to the
    // vendored .proto files that changes the wire format fails here.

    void heartbeat_is_an_empty_heartbeat_message()
    {
        // ToRadio.heartbeat = 7, a message (wire type 2): tag 0x3a, length 0
        MeshtasticProtocol proto;
        QCOMPARE(proto.createHeartbeatPacket().toHex(), QByteArray("94c300023a00"));
    }

    void reboot_uses_reboot_seconds_field_97()
    {
        // AdminMessage.reboot_seconds = 97, varint: tag (97<<3)|0 = 0x88 0x06.
        // Field 95 is reboot_ota_seconds, which reboots into the OTA loader.
        MeshtasticProtocol proto;
        QCOMPARE(adminPayloadOf(proto.createRebootPacket(1, 1, 5)).toHex(), QByteArray("880605"));
    }

    void set_channel_uses_field_33_and_keeps_id_and_precision()
    {
        MeshtasticProtocol proto;
        QVariantMap cfg;
        cfg["role"] = 2;
        cfg["name"] = "x";
        cfg["psk"] = QByteArray(1, '\x01');
        cfg["channelId"] = 0x12345678u;
        cfg["positionPrecision"] = 13u;
        const QByteArray admin = adminPayloadOf(proto.createChannelConfigPacket(1, 1, 1, cfg));

        // AdminMessage.set_channel = 33, length-delimited: tag 0x8a 0x02.
        // Field 32 is set_owner.
        QVERIFY2(admin.startsWith(QByteArray::fromHex("8a02")), admin.toHex().constData());
        // ChannelSettings.id = 4 is fixed32: tag 0x25 + 4 little-endian bytes
        QVERIFY2(admin.contains(QByteArray::fromHex("2578563412")), admin.toHex().constData());
        // ChannelSettings.module_settings = 7 { position_precision = 1: 13 }
        QVERIFY2(admin.contains(QByteArray::fromHex("3a02080d")), admin.toHex().constData());
    }

    void decodes_channel_id_and_module_settings_from_device()
    {
        // FromRadio.channel = 10 { index 1, settings { psk 01, name "x",
        //   id fixed32 0x12345678, module_settings { position_precision 13 } },
        //   role SECONDARY }
        const QByteArray settings = QByteArray::fromHex("120101" "1a0178" "2578563412" "3a02080d");
        QByteArray channel = QByteArray::fromHex("0801") + QByteArray::fromHex("12")
                             + QByteArray(1, static_cast<char>(settings.size())) + settings
                             + QByteArray::fromHex("1802");
        QByteArray fromRadio = QByteArray::fromHex("52") + QByteArray(1, static_cast<char>(channel.size())) + channel;

        MeshtasticProtocol proto;
        QSignalSpy spy(&proto, &MeshtasticProtocol::packetReceived);
        proto.processIncomingData(makeFrame(fromRadio.toStdString()));

        QCOMPARE(spy.count(), 1);
        auto pkt = spy[0][0].value<MeshtasticProtocol::DecodedPacket>();
        QCOMPARE(pkt.type, MeshtasticProtocol::PacketType::Channel);
        QCOMPARE(pkt.fields["index"].toInt(), 1);
        QCOMPARE(pkt.fields["role"].toInt(), 2);
        QCOMPARE(pkt.fields["name"].toString(), QString("x"));
        QCOMPARE(pkt.fields["channelId"].toUInt(), 0x12345678u);
        QCOMPARE(pkt.fields["positionPrecision"].toUInt(), 13u);
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
