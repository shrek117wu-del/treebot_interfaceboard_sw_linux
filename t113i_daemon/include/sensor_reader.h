#pragma once
/**
 * @file sensor_reader.h
 * @brief Periodic ADC / sensor reading via Linux IIO sysfs interface.
 *
 * SensorReader runs a background thread that samples ADC channels at the
 * configured rate and invokes a callback with the aggregated payload.
 */

#include "protocol.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// ADC channel descriptor
// ---------------------------------------------------------------------------
struct AdcChannel {
    int         index{0};
    std::string sysfs_path;   // e.g. /sys/bus/iio/devices/iio:device0/in_voltage0_raw
    float       scale{1.0f};  // raw * scale + offset = physical value
    float       offset{0.0f};
    std::string unit;         // informational
};

// ---------------------------------------------------------------------------
// SensorReader
// ---------------------------------------------------------------------------
class SensorReader {
public:
    // hz: sampling frequency (1–100 Hz)
    explicit SensorReader(int hz = 10);
    ~SensorReader();

    void add_adc_channel(const AdcChannel& ch);

    bool start();
    void stop();

    using DataCallback = std::function<void(const SensorDataPayload&)>;
    void set_callback(DataCallback cb);

private:
    int                      hz_;
    std::vector<AdcChannel>  channels_;
    DataCallback             callback_;
    std::mutex               cb_mutex_;

    std::atomic<bool>        running_{false};
    std::thread              thread_;

    void sample_loop();
    bool read_raw(const AdcChannel& ch, uint16_t& out) const;
};
