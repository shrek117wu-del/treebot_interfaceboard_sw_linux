/**
 * @file gpu_upload_node.cpp
 * @brief ROS2 节点：使用 CUDA 将相机帧异步上传到 GPU，测量资源占用。
 *
 * 订阅双相机 4 路话题（BEST_EFFORT QoS），使用 cudaMemcpyAsync +
 * cudaStreamSynchronize 上传每帧到 GPU，统计 FPS / 上传耗时 / 延迟 / 带宽，
 * 每 5 秒打印报告并写入 /tmp/gpu_upload_bench.csv。
 *
 * 硬件环境：
 *   - Wrist Camera: Orbbec Gemini 330 (USB 3.x → x86)
 *   - Head Camera:  Orbbec Gemini 305 (USB 3.x → x86)
 *   - GPU:          RTX 4090 (前期) / Thor (最终)
 *   - ROS2:         Humble (Ubuntu 22.04)
 */

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

// ─── CUDA 错误检查宏 ──────────────────────────────────────────────────────────
#define CUDA_CHECK(call)                                                         \
  do {                                                                           \
    cudaError_t err = (call);                                                    \
    if (err != cudaSuccess) {                                                    \
      RCLCPP_ERROR(rclcpp::get_logger("gpu_upload_node"),                        \
                   "CUDA 错误 %s:%d  %s",                                         \
                   __FILE__, __LINE__, cudaGetErrorString(err));                  \
    }                                                                            \
  } while (0)

// ─── 常量 ─────────────────────────────────────────────────────────────────────
static constexpr size_t  kWindowSize      = 300;          // 滑动窗口帧数
static constexpr int     kReportIntervalS = 5;            // 报告周期（秒）
static constexpr size_t  kNumSlots        = 4;            // GPU buffer 池槽位数
static constexpr size_t  kInitSlotBytes   = 10 * 1024 * 1024; // 每槽初始 10 MB
static const char*       kCsvPath         = "/tmp/gpu_upload_bench.csv";

// ─── 话题列表 ─────────────────────────────────────────────────────────────────
static const std::vector<std::pair<std::string, std::string>> kTopics = {
  {"/wrist_camera/color/image_raw", "wrist_color"},
  {"/wrist_camera/depth/image_raw", "wrist_depth"},
  {"/head_camera/color/image_raw",  "head_color"},
  {"/head_camera/depth/image_raw",  "head_depth"},
};

// ─── 每流统计结构体 ───────────────────────────────────────────────────────────
struct StreamStats {
  std::string   name;
  uint64_t      total_frames   = 0;
  uint64_t      total_bytes    = 0;
  // 上传耗时（微秒），滑动窗口
  std::deque<double> upload_times_us;
  // 端到端延迟（毫秒），滑动窗口
  std::deque<double> latencies_ms;
  // 接收 wall-clock 时间（纳秒），用于计算 FPS
  std::deque<int64_t> recv_times_ns;

  void record(double upload_us, double latency_ms,
              int64_t recv_ns, size_t bytes) {
    total_frames++;
    total_bytes += bytes;

    if (upload_times_us.size() >= kWindowSize) upload_times_us.pop_front();
    upload_times_us.push_back(upload_us);

    if (latencies_ms.size() >= kWindowSize)    latencies_ms.pop_front();
    latencies_ms.push_back(latency_ms);

    if (recv_times_ns.size() >= kWindowSize)   recv_times_ns.pop_front();
    recv_times_ns.push_back(recv_ns);
  }

  double fps() const {
    if (recv_times_ns.size() < 2) return 0.0;
    double span_s = (recv_times_ns.back() - recv_times_ns.front()) / 1e9;
    if (span_s <= 0) return 0.0;
    return static_cast<double>(recv_times_ns.size() - 1) / span_s;
  }

  double avg_upload_us() const {
    if (upload_times_us.empty()) return 0.0;
    return std::accumulate(upload_times_us.begin(), upload_times_us.end(), 0.0)
           / static_cast<double>(upload_times_us.size());
  }

  double avg_latency_ms() const {
    if (latencies_ms.empty()) return 0.0;
    return std::accumulate(latencies_ms.begin(), latencies_ms.end(), 0.0)
           / static_cast<double>(latencies_ms.size());
  }

  // 平均带宽 MB/s（基于窗口总字节 / 时间跨度）
  double bandwidth_mbps() const {
    if (recv_times_ns.size() < 2) return 0.0;
    double span_s = (recv_times_ns.back() - recv_times_ns.front()) / 1e9;
    if (span_s <= 0) return 0.0;
    return static_cast<double>(total_bytes) / (1024.0 * 1024.0) / span_s;
  }
};

// ─── GPU Buffer 池 ────────────────────────────────────────────────────────────
struct GpuBufferPool {
  std::vector<void*>  slots;
  std::vector<size_t> sizes;
  size_t              next_slot = 0;

  GpuBufferPool() : slots(kNumSlots, nullptr), sizes(kNumSlots, 0) {
    for (size_t i = 0; i < kNumSlots; ++i) {
      CUDA_CHECK(cudaMalloc(&slots[i], kInitSlotBytes));
      sizes[i] = kInitSlotBytes;
    }
  }

  ~GpuBufferPool() {
    for (auto* p : slots) {
      if (p) cudaFree(p);
    }
  }

  /**
   * 获取一个可容纳 need_bytes 的槽位（必要时 realloc × 2）。
   * @return <device_ptr, slot_index>
   */
  std::pair<void*, size_t> acquire(size_t need_bytes) {
    size_t idx = next_slot % kNumSlots;
    next_slot++;
    if (sizes[idx] < need_bytes) {
      // 自动扩容：× 2 直到满足需求
      size_t new_size = sizes[idx];
      while (new_size < need_bytes) new_size *= 2;
      CUDA_CHECK(cudaFree(slots[idx]));
      CUDA_CHECK(cudaMalloc(&slots[idx], new_size));
      sizes[idx] = new_size;
    }
    return {slots[idx], idx};
  }
};

// ─── 系统资源读取辅助函数 ──────────────────────────────────────────────────────

/**
 * 读取 /proc/meminfo 中 MemTotal / MemAvailable（单位 kB），
 * 返回 used_mb = (total - available) / 1024。
 */
static double read_system_ram_used_mb() {
  std::ifstream f("/proc/meminfo");
  if (!f.is_open()) return 0.0;
  long long total_kb = 0, avail_kb = 0;
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("MemTotal:", 0) == 0) {
      if (sscanf(line.c_str(), "MemTotal: %lld kB", &total_kb) != 1)  // NOLINT
        total_kb = 0;
    } else if (line.rfind("MemAvailable:", 0) == 0) {
      if (sscanf(line.c_str(), "MemAvailable: %lld kB", &avail_kb) != 1)  // NOLINT
        avail_kb = 0;
    }
  }
  if (total_kb == 0) return 0.0;
  return static_cast<double>(total_kb - avail_kb) / 1024.0;
}

/**
 * 读取 /proc/self/stat 计算进程 CPU 使用率（两次调用之差 / Hz）。
 * 简单实现：返回上次调用以来的 CPU 百分比（近似值）。
 */
static double read_process_cpu_pct() {
  static long long prev_time  = 0;
  static auto      prev_wall  = std::chrono::steady_clock::now();

  std::ifstream f("/proc/self/stat");
  if (!f.is_open()) return 0.0;

  std::string token;
  // 字段 14: utime, 15: stime（从 1 开始计数）
  int field = 0;
  long long utime = 0, stime = 0;
  while (f >> token) {
    field++;
    if (field == 14) utime = std::stoll(token);
    if (field == 15) { stime = std::stoll(token); break; }
  }

  long long cur_time = utime + stime;
  auto      cur_wall = std::chrono::steady_clock::now();
  double    wall_s   = std::chrono::duration<double>(cur_wall - prev_wall).count();
  double    cpu_pct  = 0.0;

  if (wall_s > 0.01 && prev_time > 0) {
    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100;
    cpu_pct = static_cast<double>(cur_time - prev_time)
              / (static_cast<double>(clk_tck) * wall_s) * 100.0;
  }

  prev_time = cur_time;
  prev_wall = cur_wall;
  return cpu_pct;
}

// ─── 主节点 ───────────────────────────────────────────────────────────────────
class GpuUploadNode : public rclcpp::Node {
public:
  GpuUploadNode()
    : Node("gpu_upload_node"),
      buffer_pool_(std::make_unique<GpuBufferPool>())
  {
    // 打印 GPU 型号和显存
    print_gpu_info();

    // QoS：BEST_EFFORT + depth=1
    rclcpp::QoS qos(1);
    qos.best_effort();

    // 为每个流创建统计和订阅
    for (auto& [topic, stream] : kTopics) {
      stats_map_[stream] = StreamStats{stream};
      auto sub = create_subscription<sensor_msgs::msg::Image>(
        topic, qos,
        [this, stream](const sensor_msgs::msg::Image::SharedPtr msg) {
          image_callback(msg, stream);
        });
      subs_.push_back(sub);
      RCLCPP_INFO(get_logger(), "  订阅: %s → %s", topic.c_str(), stream.c_str());
    }

    // 创建 CUDA 流（异步上传）
    CUDA_CHECK(cudaStreamCreate(&cuda_stream_));

    // 初始化 CSV
    init_csv();

    // 每 5 秒打印报告
    timer_ = create_wall_timer(
      std::chrono::seconds(kReportIntervalS),
      std::bind(&GpuUploadNode::report_callback, this));

    RCLCPP_INFO(get_logger(), "GpuUploadNode 已启动，等待图像话题 …");
  }

  ~GpuUploadNode() override {
    if (cuda_stream_) cudaStreamDestroy(cuda_stream_);
    if (csv_file_.is_open()) csv_file_.close();
  }

private:
  // ── GPU 信息打印 ─────────────────────────────────────────────────────────
  void print_gpu_info() {
    int device_count = 0;
    CUDA_CHECK(cudaGetDeviceCount(&device_count));
    if (device_count == 0) {
      RCLCPP_WARN(get_logger(), "未检测到 CUDA 设备！");
      return;
    }
    cudaDeviceProp prop{};
    CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
    RCLCPP_INFO(get_logger(), "GPU 设备: %s", prop.name);
    RCLCPP_INFO(get_logger(), "  显存总量: %.1f MB",
                static_cast<double>(prop.totalGlobalMem) / (1024.0 * 1024.0));
    RCLCPP_INFO(get_logger(), "  Compute Capability: %d.%d",
                prop.major, prop.minor);
  }

  // ── CSV 初始化 ───────────────────────────────────────────────────────────
  void init_csv() {
    bool write_header = !std::ifstream(kCsvPath).good();
    csv_file_.open(kCsvPath, std::ios::app);
    if (write_header) {
      csv_file_ << "timestamp,stream,fps,avg_upload_us,avg_latency_ms,"
                   "gpu_mem_used_mb,gpu_util_pct,cpu_pct,ram_used_mb\n";
      csv_file_.flush();
    }
  }

  // ── 图像回调：上传帧到 GPU ────────────────────────────────────────────────
  void image_callback(const sensor_msgs::msg::Image::SharedPtr msg,
                      const std::string& stream) {
    int64_t recv_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();

    // 计算端到端延迟（ms）
    int64_t stamp_ns = static_cast<int64_t>(msg->header.stamp.sec) * 1000000000LL
                       + msg->header.stamp.nanosec;
    double latency_ms = static_cast<double>(recv_ns - stamp_ns) / 1e6;

    // 帧数据大小
    size_t frame_bytes = msg->data.size();

    // 从 buffer 池获取槽位
    auto [d_ptr, slot_idx] = buffer_pool_->acquire(frame_bytes);
    (void)slot_idx;

    // 计时：异步上传到 GPU
    auto t0 = std::chrono::high_resolution_clock::now();
    CUDA_CHECK(cudaMemcpyAsync(d_ptr, msg->data.data(),
                               frame_bytes, cudaMemcpyHostToDevice,
                               cuda_stream_));
    CUDA_CHECK(cudaStreamSynchronize(cuda_stream_));
    auto t1 = std::chrono::high_resolution_clock::now();

    double upload_us = std::chrono::duration<double, std::micro>(t1 - t0).count();

    // 记录统计（互斥保护）
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_map_[stream].record(upload_us, latency_ms, recv_ns, frame_bytes);
  }

  // ── GPU 显存查询 ─────────────────────────────────────────────────────────
  static double query_gpu_mem_used_mb() {
    size_t free_bytes = 0, total_bytes = 0;
    cudaError_t err = cudaMemGetInfo(&free_bytes, &total_bytes);
    if (err != cudaSuccess) return 0.0;
    return static_cast<double>(total_bytes - free_bytes) / (1024.0 * 1024.0);
  }

  // ── 报告回调 ─────────────────────────────────────────────────────────────
  void report_callback() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t   = system_clock::to_time_t(now);
    char ts_buf[32];
    std::strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));

    double gpu_mem_mb = query_gpu_mem_used_mb();
    double ram_mb     = read_system_ram_used_mb();
    double cpu_pct    = read_process_cpu_pct();

    // nvidia-smi 查询 GPU 利用率（fallback = 0）
    double gpu_util = 0.0;
    {
      FILE* pipe = popen(
        "nvidia-smi --query-gpu=utilization.gpu "
        "--format=csv,noheader,nounits 2>/dev/null", "r");
      if (pipe) {
        char buf[32] = {};
        if (fgets(buf, sizeof(buf), pipe)) {
          char* endptr = nullptr;
          double val = std::strtod(buf, &endptr);
          if (endptr != buf) gpu_util = val;
        }
        pclose(pipe);
      }
    }

    std::string sep(72, '-');
    RCLCPP_INFO(get_logger(), "%s", sep.c_str());
    RCLCPP_INFO(get_logger(),
                "[GpuUpload] 时间: %s | GPU显存: %.1f MB | "
                "GPU利用率: %.1f%% | 系统RAM: %.1f MB | 进程CPU: %.1f%%",
                ts_buf, gpu_mem_mb, gpu_util, ram_mb, cpu_pct);
    RCLCPP_INFO(get_logger(),
                "  %-14s %6s %12s %12s %12s",
                "流名称", "FPS", "上传(us)", "延迟(ms)", "带宽(MB/s)");

    std::lock_guard<std::mutex> lock(stats_mutex_);
    for (auto& [stream, st] : stats_map_) {
      double fps     = st.fps();
      double upl_us  = st.avg_upload_us();
      double lat_ms  = st.avg_latency_ms();
      double bw_mbs  = st.bandwidth_mbps();

      RCLCPP_INFO(get_logger(),
                  "  %-14s %6.1f %12.2f %12.3f %12.2f",
                  stream.c_str(), fps, upl_us, lat_ms, bw_mbs);

      // 写入 CSV
      if (csv_file_.is_open()) {
        csv_file_ << ts_buf << ","
                  << stream << ","
                  << fps << ","
                  << upl_us << ","
                  << lat_ms << ","
                  << gpu_mem_mb << ","
                  << gpu_util << ","
                  << cpu_pct << ","
                  << ram_mb << "\n";
      }
    }
    if (csv_file_.is_open()) csv_file_.flush();
    RCLCPP_INFO(get_logger(), "%s", sep.c_str());
  }

  // ── 成员变量 ─────────────────────────────────────────────────────────────
  std::unique_ptr<GpuBufferPool>      buffer_pool_;
  cudaStream_t                        cuda_stream_ = nullptr;
  std::vector<rclcpp::SubscriptionBase::SharedPtr> subs_;
  std::unordered_map<std::string, StreamStats>     stats_map_;
  std::mutex                          stats_mutex_;
  rclcpp::TimerBase::SharedPtr        timer_;
  std::ofstream                       csv_file_;
};

// ─── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<GpuUploadNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
