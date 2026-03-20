#include "seeway_interface_driver/comm_handler.hpp"
#include <cstring>

namespace seeway_interface_driver {

size_t FrameCodec::encode(MsgId id, uint16_t seq, const uint8_t* payload,
                          uint16_t payload_len, uint8_t* out, size_t cap)
{
    size_t frame_len = PROTO_HEADER_TOT + payload_len + PROTO_CRC_SZ;
    if (frame_len > cap) return 0;
    size_t i = 0;
    out[i++] = PROTO_HEADER_0;
    out[i++] = PROTO_HEADER_1;
    out[i++] = static_cast<uint8_t>(id);
    out[i++] = static_cast<uint8_t>(seq & 0xFF);
    out[i++] = static_cast<uint8_t>(seq >> 8);
    out[i++] = static_cast<uint8_t>(payload_len & 0xFF);
    out[i++] = static_cast<uint8_t>(payload_len >> 8);
    out[i++] = 0x00;
    out[i++] = 0x00;
    if (payload_len > 0 && payload) {
        memcpy(out + i, payload, payload_len);
        i += payload_len;
    }
    uint16_t crc = crc16_ccitt(out + 2, PROTO_HEADER_TOT - 2 + payload_len);
    out[i++] = static_cast<uint8_t>(crc & 0xFF);
    out[i++] = static_cast<uint8_t>(crc >> 8);
    return i;
}

void FrameCodec::feed(const uint8_t* data, size_t len, const FrameCallback& cb)
{
    buf_.insert(buf_.end(), data, data + len);
    while (try_parse(cb)) {}
    if (buf_.size() > BUF_CAP) {
        buf_.erase(buf_.begin(), buf_.begin() + buf_.size() - BUF_CAP);
    }
}

bool FrameCodec::try_parse(const FrameCallback& cb)
{
    while (buf_.size() >= 2 &&
           !(buf_[0] == PROTO_HEADER_0 && buf_[1] == PROTO_HEADER_1)) {
        buf_.erase(buf_.begin());
    }
    if (buf_.size() < static_cast<size_t>(PROTO_HEADER_TOT)) return false;

    uint8_t  msg_id  = buf_[2];
    uint16_t seq     = static_cast<uint16_t>(buf_[3]) |
                       (static_cast<uint16_t>(buf_[4]) << 8);
    uint16_t pay_len = static_cast<uint16_t>(buf_[5]) |
                       (static_cast<uint16_t>(buf_[6]) << 8);

    if (pay_len > PROTO_MAX_PAYLOAD) {
        buf_.erase(buf_.begin());
        return true;
    }
    size_t total = PROTO_HEADER_TOT + pay_len + PROTO_CRC_SZ;
    if (buf_.size() < total) return false;

    uint16_t crc_calc = crc16_ccitt(buf_.data() + 2,
                                    PROTO_HEADER_TOT - 2 + pay_len);
    uint16_t crc_recv = static_cast<uint16_t>(buf_[total - 2]) |
                        (static_cast<uint16_t>(buf_[total - 1]) << 8);
    if (crc_calc != crc_recv) {
        buf_.erase(buf_.begin());
        return true;
    }

    cb(static_cast<MsgId>(msg_id), seq,
       buf_.data() + PROTO_HEADER_TOT, pay_len);
    buf_.erase(buf_.begin(), buf_.begin() + total);
    return true;
}

}  // namespace seeway_interface_driver
