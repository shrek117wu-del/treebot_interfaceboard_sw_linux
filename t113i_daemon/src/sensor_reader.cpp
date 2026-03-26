/**
 * @file sensor_reader.cpp
 * @brief SensorReader implementation – samples IIO ADC channels periodically.
 */

#include "sensor_reader.h"
#include "logger.h"

#include <chrono>
#include <fstream>
#include <stdexcept>

SensorReader::SensorReader(int hz)
    : hz_(hz > 0 ? hz : 10)
{}

SensorReader::~SensorReader() { stop(); }

void SensorReader::add_adc_channel(const AdcChannel& ch) {
    channels_.push_back(ch);
}

void SensorReader::set_callback(DataCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    callback_ = std::move(cb);
}

bool SensorReader::start() {
    running_ = true;
    thread_  = std::thread(&SensorReader::sample_loop, this);
    Logger::info("SensorReader",
        "Started at " + std::to_string(hz_) + " Hz with " +
        std::to_string(channels_.size()) + " channel(s)");
    return true;
}

void SensorReader::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

bool SensorReader::read_raw(const AdcChannel& ch, uint16_t& out) const {
    std::ifstream f(ch.sysfs_path);
    if (!f.is_open()) return false;
    int raw = 0;
    f >> raw;
    if (!f) return false;
    out = static_cast<uint16_t>(raw);
    return true;
}

void SensorReader::sample_loop() {
    using namespace std::chrono;
    const auto period = milliseconds(1000 / hz_);

    while (running_) {
        auto wake_at = steady_clock::now() + period;

        SensorDataPayload data{};
        data.channel_count = static_cast<uint8_t>(
            channels_.size() < SENSOR_ADC_CHANNELS
                ? channels_.size()
                : SENSOR_ADC_CHANNELS);

        for (size_t i = 0; i < data.channel_count; ++i) {
            const auto& ch = channels_[i];
            uint16_t raw = 0;
            if (!read_raw(ch, raw)) {
                Logger::debug("SensorReader",
                    "Cannot read ADC channel " + std::to_string(ch.index));
            }
            data.adc_raw[ch.index < SENSOR_ADC_CHANNELS ? ch.index : i] = raw;
        }

        {
            std::lock_guard<std::mutex> lk(cb_mutex_);
            if (callback_) callback_(data);
        }

        std::this_thread::sleep_until(wake_at);
    }
}
