#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace walkie::protocol {

constexpr uint32_t kMagic = 0x574B5431U;  // "WKT1"
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderSize = 26;
constexpr size_t kMaxWireSize = 250;
constexpr size_t kMaxPayloadSize = kMaxWireSize - kHeaderSize;
constexpr size_t kAudioSamples = 320;
constexpr size_t kAdpcmBytes = kAudioSamples / 2;
constexpr size_t kAudioPayloadSize = 4 + kAdpcmBytes;
constexpr size_t kDeviceNameSize = 8;

static_assert(kHeaderSize + kAudioPayloadSize < kMaxWireSize);

enum class MessageType : uint8_t {
    Heartbeat = 1,
    TalkClaim = 2,
    TalkStart = 3,
    Audio = 4,
    TalkEnd = 5,
};

enum class DeviceState : uint8_t {
    Idle = 0,
    Requesting = 1,
    Talking = 2,
    Receiving = 3,
    Busy = 4,
};

enum class BoardType : uint8_t {
    Unknown = 0,
    StickS3 = 1,
    StopWatch = 2,
    AiPassport = 3,
    Mosaico = 4,
};

using DeviceId = std::array<uint8_t, 6>;

struct Header {
    MessageType type{MessageType::Heartbeat};
    uint8_t flags{0};
    uint8_t logical_channel{1};
    DeviceId sender_id{};
    uint32_t session_id{0};
    uint16_t sequence{0};
    uint32_t timestamp_ms{0};
    uint16_t payload_length{0};
};

struct PacketView {
    Header header{};
    const uint8_t* payload{nullptr};
};

struct Heartbeat {
    std::array<char, kDeviceNameSize> name{};
    uint8_t battery_percent{0};
    DeviceState state{DeviceState::Idle};
    BoardType board{BoardType::Unknown};
};

struct Claim {
    uint32_t arbitration_value{0};
};

enum class DecodeError {
    None,
    TooShort,
    TooLarge,
    BadMagic,
    BadVersion,
    BadType,
    BadChannel,
    BadPayloadSize,
    LengthMismatch,
};

bool encode(const Header& header, const uint8_t* payload, size_t payload_size,
            uint8_t* output, size_t output_capacity, size_t& output_size);
DecodeError decode(const uint8_t* data, size_t size, PacketView& packet);

bool encode_heartbeat(const Heartbeat& heartbeat, uint8_t* output, size_t capacity,
                      size_t& output_size);
bool decode_heartbeat(const PacketView& packet, Heartbeat& heartbeat);
bool encode_claim(const Claim& claim, uint8_t* output, size_t capacity, size_t& output_size);
bool decode_claim(const PacketView& packet, Claim& claim);

int compare_device_id(const DeviceId& lhs, const DeviceId& rhs);
bool is_known_type(MessageType type);
bool is_valid_payload_size(MessageType type, size_t payload_size);

}  // namespace walkie::protocol
