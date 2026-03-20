#pragma once

#include "seeway_interface_driver/protocol.hpp"
#include <functional>
#include <vector>

namespace seeway_interface_driver {

using FrameCallback = std::function<void(MsgId id, uint16_t seq, const uint8_t* payload, uint16_t len)>;

// ---------------------------------------------------------------------------
// FrameCodec – shared framing helper used by all transport implementations.
// ---------------------------------------------------------------------------
class FrameCodec {
public:
    // Encode one frame into out_buf.  Returns the number of bytes written, or
    // 0 if out_cap is too small.
    static size_t encode(MsgId id, uint16_t seq, const uint8_t* payload,
                         uint16_t payload_len, uint8_t* out_buf, size_t out_cap);

    // Feed raw bytes into the internal reassembly buffer.  Calls cb for every
    // complete, CRC-valid frame that is parsed.
    void feed(const uint8_t* data, size_t len, const FrameCallback& cb);

private:
    std::vector<uint8_t> buf_;
    static const size_t BUF_CAP = 2 * PROTO_MAX_FRAME;
    bool try_parse(const FrameCallback& cb);
};

}  // namespace seeway_interface_driver
