# HT Exporter

An ESP32 + AM2302 environmental temperature/humidity collector and Prometheus exporter.

## Features

- Read ambient temperature and humidity from an AM2302 sensor
- Expose metrics in Prometheus format
- Configure Wi-Fi and static IP via serial CLI
- Display system status through the onboard LED

## Hardware and Default Configuration

- Development board: Seeed XIAO ESP32-C6
- Sensor: AM2302
- AM2302 data pin: GPIO0
- Status LED pin: GPIO15
- HTTP service port: 9100
- Sensor sampling interval: 2 seconds

## Wi-Fi Configuration

This project supports two methods:

1. Write credentials to NVS at runtime via CLI (preferred)
2. Provide default credentials at build time

### Configure via Serial CLI

#### Configure Wi-Fi credentials

```text
# Set Wi-Fi credentials
wifi_set <ssid> <pass>
# Show current credentials (password masked)
wifi_show
# Clear credentials
wifi_clear
```

#### Configure Static IP

```text
# Set static IP
ip_set <ip> <gw> <mask>
# Example
ip_set 192.168.1.50 192.168.1.1 255.255.255.0
# Clear static IP (restore DHCP)
ip_clear
```

### Build-Time Default Credentials

Uncomment and update `build_flags` in `platformio.ini`:

```ini
build_flags =
  -DWIFI_DEFAULT_SSID="MySSID"
  -DWIFI_DEFAULT_PASS="MyPassword"
```

## Prometheus Metrics

The following metrics are currently exported:

- `dht_humidity_percent` (gauge): Relative humidity in %
- `dht_temperature_celsius` (gauge): Temperature in degrees Celsius

Example Prometheus scrape config:

```yaml
scrape_configs:
  - job_name: "ht-exporter"
    static_configs:
      - targets: ["192.168.1.50:9100"]
```

## Serial CLI Commands

Available commands:

- `help`: Show command help
- `status`: Show sensor task status and latest reading
- `wifi_set <ssid> <pass>`: Set Wi-Fi credentials
- `wifi_show`: Show saved credentials (password masked)
- `wifi_clear`: Clear saved credentials
- `ip_set <ip> <gw> <mask>`: Set static IP
- `ip_clear`: Clear static IP (restore DHCP)

## LED Status

- During initialization, the onboard LED indicates system status:
  - `BOOT`: Slow blink
  - `WIFI_CONNECTING`: Fast blink
  - `WIFI_CONNECTED`: Solid on
  - `ERROR`: Double blink
  - `OFF`: Off
- After the system is initialized, a short pulse blink is emitted after each successful sensor read.
