#pragma once
#include <cstdint>
#include <string>

// 协议魔数
static constexpr uint32_t PROTO_MAGIC = 0x4C414E43; // "LANC"

// 应用层协议结构体
#pragma pack(push,1)
struct AppHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  msg_type;
    uint16_t flags;
    uint32_t payload_len;
    uint32_t crc32;
};
#pragma pack(pop)

// 消息类型
enum : uint8_t {
    MT_TEXT = 1,
    MT_FILE_META = 2,
    MT_FILE_CHUNK = 3,
    MT_ACK = 4,
    MT_INVALID_SEMANTIC = 5,
    MT_HEARTBEAT = 6
};
