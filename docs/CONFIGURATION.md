# Configuration Reference

The daemon reads an INI-style configuration file (default:
`/etc/seeway_interface/daemon.conf`).  The file supports:
- `[section]` headers
- `key=value` pairs (whitespace around `=` is stripped)
- `#` comment lines

---

## `[communication]`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `transport` | string | `tcp` | Transport type: `tcp` or `uart` |
| `server_ip` | string | `127.0.0.1` | Jetson IP address (TCP mode) |
| `server_port` | int | `9000` | TCP port |
| `uart_device` | string | `/dev/ttyS3` | UART device path |
| `baud_rate` | int | `115200` | UART baud rate |
| `reconnect_ms` | int | `2000` | Reconnect interval on TCP loss (ms) |
| `heartbeat_timeout_ms` | int | `5000` | Heartbeat timeout before TIMEOUT state (ms) |

---

## `[gpio]`

Each GPIO pin is declared as a `pin=` entry with comma-separated fields:

```
pin=bank,pin,linux_num,is_output,active_low,label
```

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `bank` | int | 0–7 | GPIO bank number |
| `pin` | int | 0–31 | Pin within the bank |
| `linux_num` | int | 0–511 | Linux GPIO number (for sysfs) |
| `is_output` | bool | 0/1 | 1 = output, 0 = input |
| `active_low` | bool | 0/1 | 1 = active-low polarity |
| `label` | string | — | Human-readable label |

**Example:**
```ini
[gpio]
pin=0,5,64,1,0,led_power
pin=1,3,96,0,1,estop_in
```

---

## `[adc]`

Each ADC channel is declared as a `channel=` entry:

```
channel=index,sysfs_path,scale,offset,unit
```

| Field | Type | Description |
|-------|------|-------------|
| `index` | int | Channel index (0-based) |
| `sysfs_path` | path | Full sysfs path to the raw ADC file |
| `scale` | float | Multiplier applied to raw value |
| `offset` | float | Additive offset after scaling |
| `unit` | string | Unit label (e.g., `V`, `mA`, `°C`) |

**Example:**
```ini
[adc]
channel=0,/sys/bus/iio/devices/iio:device0/in_voltage0_raw,0.001221,0.0,V
channel=1,/sys/bus/iio/devices/iio:device0/in_voltage1_raw,0.001221,0.0,V
```

---

## `[power]`

Each power rail is declared as a `rail=` entry:

```
rail=on_cmd_id(hex),off_cmd_id(hex),gpio_num,active_low,min_on_ms,sequence_delay_ms
```

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `on_cmd_id` | hex | 0x01–0xFF | PowerCmd for ON |
| `off_cmd_id` | hex | 0x01–0xFF | PowerCmd for OFF |
| `gpio_num` | int | 0–511 | Linux GPIO number controlling the rail |
| `active_low` | bool | 0/1 | 1 = GPIO low = rail ON |
| `min_on_ms` | int | 100–60000 | Minimum ON time before OFF (ms) |
| `sequence_delay_ms` | int | 0–10000 | Delay between sequential operations (ms) |

**Example:**
```ini
[power]
rail=0x01,0x02,64,0,5000,1000
rail=0x11,0x12,65,0,3000,500
```

---

## `[logging]`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `log_file` | path | (empty) | Log file path; empty = stderr only |
| `log_level` | int | `1` | 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=FATAL |

**Example:**
```ini
[logging]
log_file=/var/log/seeway_daemon.log
log_level=1
```

---

## Complete Example

```ini
[communication]
transport=tcp
server_ip=192.168.1.100
server_port=9000
reconnect_ms=2000
heartbeat_timeout_ms=5000

[gpio]
pin=0,5,64,1,0,jetson_power_ctrl
pin=0,6,65,1,0,arm_power_ctrl
pin=1,0,96,0,1,estop_input

[adc]
channel=0,/sys/bus/iio/devices/iio:device0/in_voltage0_raw,0.001221,0.0,V
channel=1,/sys/bus/iio/devices/iio:device0/in_voltage1_raw,0.001221,0.0,V

[power]
rail=0x01,0x02,64,0,5000,1000
rail=0x11,0x12,65,0,3000,500

[logging]
log_file=/var/log/seeway_daemon.log
log_level=1
```

---

## Validation Rules

- `gpio_num` must be in range 0–511
- `min_on_ms` must be in range 100–60 000 ms
- `log_level` must be in range 0–4
- `transport` must be `tcp` or `uart`
- `sysfs_path` for ADC channels must begin with `/sys/`
