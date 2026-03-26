# Troubleshooting Guide

## Connection Issues

### Daemon cannot connect to Jetson

**Symptoms:** Log shows `"check_health: TIMEOUT"` or `"reconnect attempt"`.

**Checks:**
1. Verify Jetson IP and port in `daemon.conf`:
   ```ini
   [communication]
   server_ip=192.168.1.100
   server_port=9000
   ```
2. Test connectivity from T113i:
   ```bash
   nc -zv 192.168.1.100 9000
   ```
3. Ensure `seeway_driver_node` is running on Jetson:
   ```bash
   ros2 node list | grep seeway
   ```
4. Check firewall rules on Jetson:
   ```bash
   sudo ufw status
   iptables -L -n | grep 9000
   ```

---

### Heartbeat timeout fires immediately

**Symptom:** ConnectionMonitor transitions to `TIMEOUT` immediately after `CONNECTED`.

**Cause:** `heartbeat_timeout_ms` is too low, or Jetson is not sending heartbeats.

**Fix:**
```ini
[communication]
heartbeat_timeout_ms=10000   # increase to 10 s
```

---

### UART connection drops randomly

**Checks:**
1. Verify baud rate matches both sides (`115200` by default)
2. Check for hardware flow control:
   ```bash
   stty -F /dev/ttyS3 | grep -E "crtscts|ixon"
   ```
3. Monitor signal quality:
   ```bash
   dmesg | grep tty
   ```

---

## GPIO / ADC Diagnostics

### GPIO sysfs export fails

**Symptom:** `GpioController start() failed`

**Fix:**
```bash
# Export the GPIO manually to test
echo 64 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio64/direction
cat /sys/class/gpio/gpio64/value
```

If the GPIO is already claimed by another driver, check:
```bash
cat /sys/kernel/debug/pinctrl/*/pinmux-pins 2>/dev/null | grep gpio64
```

---

### ADC reads return 0 or wrong values

**Checks:**
1. Verify the sysfs path:
   ```bash
   cat /sys/bus/iio/devices/iio:device0/in_voltage0_raw
   ```
2. Check the scale factor in `daemon.conf` – typical IIO raw values need
   division by 1000 to get volts.

---

## Task Execution Issues

### Task queue fills up (QUEUE_FULL)

**Symptom:** Log shows `"QUEUE_FULL – dropping TASK_COMMAND seq=N"`

**Cause:** Jetson is sending tasks faster than T113i can execute them, or
tasks are taking too long (default timeout: 30 s).

**Fix:**
- Reduce task dispatch rate on Jetson side
- Check if `TASK_CUSTOM` tasks have stuck blocking calls

---

### Tasks time out without completing

**Symptom:** `TaskResponsePayload.result = 0` with message `"task timeout"`

**Checks:**
1. Verify `TaskContext` pointers are not null (check startup logs)
2. For `TASK_ARM_HOME`: verify the arm hardware is responsive
3. For `TASK_ESTOP`: verify E-stop GPIO is connected

---

## Memory Leak Detection

```bash
# Run all unit tests under Valgrind
cd build_tests
for t in test_logger test_connection_monitor test_module_initializer \
          test_protocol test_utils test_config_loader; do
    valgrind --leak-check=full --error-exitcode=1 ./${t} 2>&1 | tail -5
done
```

---

## Performance Profiling

### CPU profiling with `perf`

```bash
# Build with debug symbols
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j$(nproc)

# Record for 30 s
perf record -g ./seeway_interface_daemon
perf report
```

### Memory profiling with massif

```bash
valgrind --tool=massif --pages-as-heap=yes \
         ./seeway_interface_daemon --config daemon.conf
ms_print massif.out.* | head -60
```

---

## Logging Diagnostics

### No log file created

**Checks:**
1. Verify `log_file` path in `daemon.conf` is set and writable:
   ```bash
   touch /var/log/seeway_daemon.log
   chmod 644 /var/log/seeway_daemon.log
   ```
2. Ensure `log_level` is set low enough (0 = DEBUG, 1 = INFO)

---

### Log file not rotating

Log rotation occurs when the file exceeds **10 MB**.  Force rotation:
```bash
# Rename current log (daemon will recreate it)
mv /var/log/seeway_daemon.log /var/log/seeway_daemon.log.old
```

---

## Network Diagnostics

### Simulate packet loss to test recovery

Using `tc` (requires `iproute2`):
```bash
# Add 5% packet loss to loopback (test environment)
sudo tc qdisc add dev lo root netem loss 5%

# Run integration tests
./integration_test_error_recovery

# Remove
sudo tc qdisc del dev lo root
```

### Check TCP retransmit counters

```bash
ss -s
netstat -s | grep -i retransmit
```

---

## CI/CD Failures

### Unit tests fail in GitHub Actions but pass locally

1. Check the GTest version: CI uses `libgtest-dev` from Ubuntu 22.04 repos
2. Check for timing-sensitive tests: increase timeouts in `CMakeLists.txt`

### Cross-compilation fails

```
error: arm-linux-gnueabihf-g++: command not found
```

Add to CI:
```yaml
- name: Install ARM toolchain
  run: sudo apt-get install -y gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
```
