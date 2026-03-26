# Configuration Reference

All configuration is read from an INI-style file (default:
`/etc/seeway_interface/daemon.conf`).

Pass a custom path as the first command-line argument:
```bash
seeway_interface_daemon /path/to/daemon.conf
```

---

## Format

```ini
[section]
key = value     # leading/trailing whitespace stripped
# comment       # lines starting with # or ; are ignored
```

---

## [communication]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `transport` | string | `tcp` | `tcp` or `uart` |
| `server_ip` | string | `127.0.0.1` | Jetson server IP (TCP mode) |
| `server_port` | int | `9000` | Jetson server port (TCP mode) |
| `uart_device` | string | `/dev/ttyS3` | Serial device path (UART mode) |
| `baud_rate` | int | `115200` | UART baud rate |
| `reconnect_ms` | int | `2000` | Reconnect retry interval (ms) |
| `heartbeat_timeout_ms` | int | `5000` | Heartbeat timeout before reconnect (ms) |

**Example:**
```ini
[communication]
transport=tcp
server_ip=192.168.1.50
server_port=9000
reconnect_ms=2000
heartbeat_timeout_ms=5000
```

---

## [gpio]

Each key defines one GPIO pin.  Key names are arbitrary (e.g. `pin_0`, `valve_left`).

**Format:** `bank,pin,linux_num,is_output,active_low,label`

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `bank` | int | 0–3 | GPIO bank (matches `GPIO_BANK_COUNT=4`) |
| `pin` | int | any | Pin within bank |
| `linux_num` | int | 0–511 | Linux GPIO number (`/sys/class/gpio/gpioN`) |
| `is_output` | bool | `true`/`false` | `true` = output, `false` = input |
| `active_low` | bool | `true`/`false` | `true` = invert logic level |
| `label` | string | — | Human-readable name |

**Example:**
```ini
[gpio]
pin_0=0,0,10,true,false,valve_1
pin_1=0,1,11,true,false,valve_2
pin_2=2,0,64,false,false,estop_btn
```

---

## [adc]

Each key defines one ADC channel via Linux IIO sysfs.

**Format:** `index,sysfs_path,scale,offset,unit`

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `index` | int | 0–7 | Channel index (max `SENSOR_ADC_CHANNELS=8`) |
| `sysfs_path` | string | — | Path to IIO `in_voltageN_raw` file |
| `scale` | float | any | Multiply raw ADC count by this value |
| `offset` | float | any | Add to scaled value |
| `unit` | string | — | Unit label (e.g. `V`, `mA`) |

**Example:**
```ini
[adc]
ch_0=0,/sys/bus/iio/devices/iio:device0/in_voltage0_raw,0.001,0.0,V
```

---

## [power]

Each key defines one power rail controlled by a GPIO pin.

**Format:** `on_cmd_id(hex),off_cmd_id(hex),gpio_num,active_low,min_on_ms,sequence_delay_ms`

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `on_cmd_id` | uint8 hex | 0x00–0xFF | `PowerCmd` value to switch on |
| `off_cmd_id` | uint8 hex | 0x00–0xFF | `PowerCmd` value to switch off |
| `gpio_num` | int | 0–511 | Linux GPIO number for this rail |
| `active_low` | bool | — | Active-low GPIO polarity |
| `min_on_ms` | uint32 | 0–600000 | Minimum on time before allowing off (ms) |
| `sequence_delay_ms` | uint32 | 0–60000 | Delay after switching (sequence gap) (ms) |

**Example:**
```ini
[power]
nx_rail=0x01,0x02,20,true,5000,1000
arm_rail=0x11,0x12,21,false,3000,500
```

---

## [logging]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `log_file` | string | `""` | Log file path; empty = stderr only |
| `log_level` | int | `1` | `0`=DEBUG `1`=INFO `2`=WARN `3`=ERROR `4`=FATAL |

**Log file rotation:** When `log_file` reaches 10 MB it is renamed to
`<log_file>.1` and a new file is opened.

**Example:**
```ini
[logging]
log_file=/var/log/seeway_daemon.log
log_level=1
```

---

## Validation Rules

- GPIO `linux_num`: 0–511
- GPIO `bank`: 0–3
- ADC `index`: 0–7
- Power `min_on_ms`: 0–600,000
- Power `sequence_delay_ms`: 0–60,000
- `server_port`, `baud_rate`, `reconnect_ms`, `heartbeat_timeout_ms`: valid integers

Validation failures cause `load_daemon_config()` to return `false` and the
daemon to exit with code 1.

---

## Optimization Tips

- **CPU**: Use `reconnect_ms ≥ 500` to avoid tight reconnect loops
- **Heartbeat**: Set `heartbeat_timeout_ms` to 2–3× the heartbeat period
- **Logging**: Use `log_level=2` (WARN) or `log_level=3` (ERROR) in production
- **Power rails**: Set `min_on_ms` conservatively to protect hardware
