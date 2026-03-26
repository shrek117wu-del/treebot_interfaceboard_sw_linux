# Troubleshooting Guide

Common problems and solutions for the Seeway Interface Daemon.

---

## Startup Issues

### Q: Daemon fails with "Failed to load configuration"
**A:** Check the config file path and permissions:
```bash
ls -l /etc/seeway_interface/daemon.conf
cat /etc/seeway_interface/daemon.conf
```
Validate config manually:
```bash
seeway_interface_daemon /path/to/daemon.conf
# Look for [config] error lines on stderr
```

### Q: "Cannot open log file" warning
**A:** The directory must exist and be writable:
```bash
mkdir -p /var/log
touch /var/log/seeway_daemon.log
chmod 644 /var/log/seeway_daemon.log
```

### Q: "GpioController start failed"
**A:** Check that GPIO sysfs is accessible:
```bash
ls /sys/class/gpio/
echo 10 > /sys/class/gpio/export
cat /sys/class/gpio/gpio10/direction
```
Run the daemon as root or add the user to the `gpio` group.

### Q: "SerialComm failed initial connect"
**A:** This is a warning, not a fatal error.  The daemon will retry automatically.
Check that the Jetson server is running and `server_ip`/`server_port` are correct:
```bash
nc -zv 192.168.1.50 9000
```

---

## Connection Issues

### Q: Daemon shows "DISCONNECTED" state permanently
**A:** Check network connectivity and firewall:
```bash
ping 192.168.1.50
iptables -L INPUT | grep 9000
# If blocked:
sudo iptables -A INPUT -p tcp --dport 9000 -j ACCEPT
```

### Q: "Heartbeat timeout (5000 ms)" – keeps disconnecting
**A:** The Jetson is not sending heartbeats.  Check the ROS2 driver:
```bash
# On Jetson:
ros2 topic hz /seeway/heartbeat
```
Increase `heartbeat_timeout_ms` in `daemon.conf` if network jitter is high:
```ini
heartbeat_timeout_ms=10000
```

### Q: Reconnect attempts keep increasing
**A:** The TCP server on Jetson is not listening:
```bash
# On Jetson, verify the driver node is running:
ros2 node list | grep seeway
```

---

## Protocol Issues

### Q: "Protocol version mismatch" in log
**A:** The T113i and Jetson are running different firmware/driver versions.
Both sides will still communicate, but features may behave differently.
Update both sides to the same version.

### Q: "MSG_TASK_COMMAND: unexpected len" warnings
**A:** The Jetson driver is sending a different `TaskCommandPayload` struct
than expected.  Ensure both sides use the same `protocol.h` version (check
`sizeof(TaskCommandPayload)` = 37 bytes).

### Q: "CRC mismatch" in log
**A:** Indicates data corruption on the wire.  Check:
- Cable quality (USB-CDC or Ethernet)
- `baud_rate` matches on both sides (UART mode)
- No intermediate devices adding framing

---

## Task Execution Issues

### Q: Tasks are rejected with "queue full"
**A:** The task executor queue (`MAX_QUEUE_DEPTH=64`) is full.  This can happen
if tasks are being sent faster than they execute:
- Reduce command rate on Jetson side
- Check if `TASK_ARM_HOME` has an unusually long sleep

### Q: Task response never arrives
**A:** Check `TASK_TIMEOUT_MS` (30,000 ms).  If the task takes longer, it will
be timed out and the response dropped.  For long tasks, break them into smaller
subtasks.

---

## Log Interpretation

### Log level reference:
```
[DEBUG] – Verbose trace (disabled in production)
[INFO ] – Normal operation events
[WARN ] – Non-fatal issues (recoverable)
[ERROR] – Operational errors (functionality affected)
[FATAL] – Emergency stop or critical failure
```

### Key log messages:
| Message | Meaning |
|---------|---------|
| `Handshake completed with peer` | Normal startup |
| `Connection lost` | TCP/UART disconnected |
| `Heartbeat timeout (N ms)` | Peer stopped sending HB |
| `Reconnect attempt #N` | Attempting to reconnect |
| `ESTOP triggered!` | Emergency stop received |
| `Task queue full` | Too many concurrent tasks |

---

## Debug Techniques

### Enable DEBUG log level:
```ini
[logging]
log_level=0
log_file=/tmp/seeway_debug.log
```

### Monitor log in real time:
```bash
tail -f /var/log/seeway_daemon.log
```

### Use mock Jetson server for testing:
```bash
python3 t113i_daemon/tests/integration/mock_jetson_server.py \
    --port 9000 --verbose
```

### Check GPIO state manually:
```bash
cat /sys/class/gpio/gpio10/value
echo 1 > /sys/class/gpio/gpio10/value
```

### Check ADC raw values:
```bash
cat /sys/bus/iio/devices/iio:device0/in_voltage0_raw
```

### Valgrind memory check:
```bash
valgrind --leak-check=full --track-origins=yes \
    ./seeway_interface_daemon daemon.conf 2>&1 | tee valgrind.log
```

---

## Network Diagnosis

### Simulate packet loss with tc/netem (Linux):
```bash
# Add 5% packet loss on outgoing interface
sudo tc qdisc add dev eth0 root netem loss 5%
# Test daemon reconnect behaviour...
sudo tc qdisc del dev eth0 root
```

### Capture frames with tcpdump:
```bash
sudo tcpdump -i eth0 -w seeway_capture.pcap \
    'tcp port 9000' &
# Run daemon...
# Analyze:
tcpdump -r seeway_capture.pcap -x | head -40
```
