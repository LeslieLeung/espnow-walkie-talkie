#include "walkie/protocol.hpp"

#include <algorithm>
#include <cstring>

namespace walkie::protocol {
namespace {

void put_u16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value >> 8);
    out[1] = static_cast<uint8_t>(value);
}

void put_u32(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value >> 24);
    out[1] = static_cast<uint8_t>(value >> 16);
    out[2] = static_cast<uint8_t>(value >> 8);
    out[3] = static_cast<uint8_t>(value);
}

uint16_t get_u16(const uint8_t* in) {
    return static_cast<uint16_t>((static_cast<uint16_t>(in[0]) << 8) | in[1]);
}

uint32_t get_u32(const uint8_t* in) {
    return (static_cast<uint32_t>(in[0]) << 24) |
           (static_cast<uint32_t>(in[1]) << 16) |
           (static_cast<uint32_t>(in[2]) << 8) | in[3];
}

}  // namespace

bool is_known_type(MessageType type) {
    const auto value = static_cast<uint8_t>(type);
    return value >= static_cast<uint8_t>(MessageType::Heartbeat) &&
           value <= static_cast<uint8_t>(MessageType::TalkEnd);
}

bool is_valid_payload_size(MessageType type, size_t payload_size) {
    switch (type) {
        case MessageType::Heartbeat: return payload_size == kDeviceNameSize + 3;
        case MessageType::TalkClaim: return payload_size == 4;
        case MessageType::TalkStart: return payload_size == 0;
        case MessageType::Audio: return payload_size == kAudioPayloadSize;
        case MessageType::TalkEnd: return payload_size == 0;
    }
    return false;
}

bool encode(const Header& header, const uint8_t* payload, size_t payload_size,
            uint8_t* output, size_t output_capacity, size_t& output_size) {
    output_size = 0;
    if (output == nullptr || payload_size > kMaxPayloadSize ||
        (payload_size != 0 && payload == nullptr) ||
        !is_known_type(header.type) || !is_valid_payload_size(header.type, payload_size) ||
        output_capacity < kHeaderSize + payload_size ||
        header.logical_channel < 1 || header.logical_channel > 4) {
        return false;
    }

    put_u32(output, kMagic);
    output[4] = kVersion;
    output[5] = static_cast<uint8_t>(header.type);
    output[6] = header.flags;
    output[7] = header.logical_channel;
    std::memcpy(output + 8, header.sender_id.data(), header.sender_id.size());
    put_u32(output + 14, header.session_id);
    put_u16(output + 18, header.sequence);
    put_u32(output + 20, header.timestamp_ms);
    put_u16(output + 24, static_cast<uint16_t>(payload_size));
    if (payload_size != 0) {
        std::memcpy(output + kHeaderSize, payload, payload_size);
    }
    output_size = kHeaderSize + payload_size;
    return true;
}

DecodeError decode(const uint8_t* data, size_t size, PacketView& packet) {
    packet = {};
    if (data == nullptr || size < kHeaderSize) return DecodeError::TooShort;
    if (size > kMaxWireSize) return DecodeError::TooLarge;
    if (get_u32(data) != kMagic) return DecodeError::BadMagic;
    if (data[4] != kVersion) return DecodeError::BadVersion;
    packet.header.type = static_cast<MessageType>(data[5]);
    if (!is_known_type(packet.header.type)) return DecodeError::BadType;
    if (data[7] < 1 || data[7] > 4) return DecodeError::BadChannel;

    packet.header.flags = data[6];
    packet.header.logical_channel = data[7];
    std::memcpy(packet.header.sender_id.data(), data + 8, packet.header.sender_id.size());
    packet.header.session_id = get_u32(data + 14);
    packet.header.sequence = get_u16(data + 18);
    packet.header.timestamp_ms = get_u32(data + 20);
    packet.header.payload_length = get_u16(data + 24);
    if (size != kHeaderSize + packet.header.payload_length) return DecodeError::LengthMismatch;
    if (!is_valid_payload_size(packet.header.type, packet.header.payload_length)) {
        return DecodeError::BadPayloadSize;
    }
    packet.payload = data + kHeaderSize;
    return DecodeError::None;
}

bool encode_heartbeat(const Heartbeat& heartbeat, uint8_t* output, size_t capacity,
                      size_t& output_size) {
    constexpr size_t kSize = kDeviceNameSize + 3;
    if (capacity < kSize || output == nullptr) return false;
    std::memcpy(output, heartbeat.name.data(), heartbeat.name.size());
    output[kDeviceNameSize] = std::min<uint8_t>(heartbeat.battery_percent, 100);
    output[kDeviceNameSize + 1] = static_cast<uint8_t>(heartbeat.state);
    output[kDeviceNameSize + 2] = static_cast<uint8_t>(heartbeat.board);
    output_size = kSize;
    return true;
}

bool decode_heartbeat(const PacketView& packet, Heartbeat& heartbeat) {
    constexpr size_t kSize = kDeviceNameSize + 3;
    if (packet.header.type != MessageType::Heartbeat || packet.header.payload_length != kSize ||
        packet.payload == nullptr) return false;
    std::memcpy(heartbeat.name.data(), packet.payload, heartbeat.name.size());
    heartbeat.name.back() = '\0';
    heartbeat.battery_percent = std::min<uint8_t>(packet.payload[kDeviceNameSize], 100);
    const auto state = packet.payload[kDeviceNameSize + 1];
    if (state > static_cast<uint8_t>(DeviceState::Busy)) return false;
    heartbeat.state = static_cast<DeviceState>(state);
    heartbeat.board = static_cast<BoardType>(packet.payload[kDeviceNameSize + 2]);
    return true;
}

bool encode_claim(const Claim& claim, uint8_t* output, size_t capacity, size_t& output_size) {
    if (output == nullptr || capacity < 4) return false;
    put_u32(output, claim.arbitration_value);
    output_size = 4;
    return true;
}

bool decode_claim(const PacketView& packet, Claim& claim) {
    if (packet.header.type != MessageType::TalkClaim || packet.header.payload_length != 4 ||
        packet.payload == nullptr) return false;
    claim.arbitration_value = get_u32(packet.payload);
    return true;
}

int compare_device_id(const DeviceId& lhs, const DeviceId& rhs) {
    return std::memcmp(lhs.data(), rhs.data(), lhs.size());
}

}  // namespace walkie::protocol
