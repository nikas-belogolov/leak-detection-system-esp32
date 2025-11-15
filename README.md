# Leak Detection System ESP32 Code

## Overview

This is the embedded firmware for the **Leak Detection System**, running on an **ESP32 microcontroller**. The system monitors water pipe networks for leaks by collecting real-time sensor data (pressure and flow measurements) and transmitting it to a backend server via MQTT protocol.

## Main Purpose

The ESP32 acts as an edge device that:
- **Collects sensor readings** at regular intervals (1-second intervals by default)
- **Communicates data** to a backend system via MQTT in an efficient binary format (protocol buffers)
- **Minimizes power consumption** through deep sleep and ULP (Ultra Low Power) coprocessor usage

## Main Components

### 1. **Sensors Module** (`sensors/`)
Manages hardware sensor initialization and data acquisition:
- **Pressure Sensor**: ADC-based analog reading on GPIO32 (ADC1 Channel 4)
  - Measures water system pressure
  - Range: 0.0440 normalized units
  - 12-bit resolution with 12dB attenuation
- **Flow Sensor**: Pulse counter on GPIO25
  - Counts water pulses to calculate flow rate
  - Calibration: 6.6 pulses per liter
  - Max flow rate: 30 LPM

Both sensors are normalized to 0-1 float values and measurements are taken every **1000ms** (1 second).

### 2. **MQTT Module** (`mqtt/`)
Handles communication with a backend MQTT broker:
- Establishes MQTT connection
- Publishes sensor data in **Protocol Buffer** binary format

### 3. **Provisioning Module** (`prov/`)
Provides WiFi network configuration capabilities:
- **BLE-based WiFi provisioning** for easy device setup
- Allows users to configure network credentials without hardcoding
- Uses WiFi Provisioning Manager from ESP-IDF

### 4. **Protocol Buffers** (`proto/`)
Efficient binary serialization for sensor data:
- **`sample_batch.proto`**: Defines message structure for batching samples
  - `Sample`: Contains timestamp, flow rate, and pressure readings
  - `SampleBatch`: Contains multiple samples for batch transmission

### 5. **SNTP Module** (`sntp/`)
Time synchronization service:
- Synchronizes device time with NTP servers
- Ensures accurate timestamps for all sensor readings

### 6. **ULP Co-processor** (`ulp/`)
Ultra Low Power processor for minimal-power operation:
- **`pulse_count.S`**: Assembler program for counting pulses in sleep mode
- **`wake_up.S`**: Handles device wake-up logic
- Allows the main CPU to sleep while still capturing flow sensor pulses

### 7. **Common Utilities** (`common/`)
Shared helper functions and device identification:
- Unique device ID generation from MAC address
- Common logging and error handling utilities

## Architecture & Data Flow

```
┌─────────────────────────────────────────────────┐
│           Hardware Sensors (GPIO)               │
│  ┌───────────────────┬──────────────────────┐   │
│  │ Pressure (GPIO32) │ Flow Pulses (GPIO25) │   │
│  └───────────────────┴──────────────────────┘   │
└────────────────────────┬────────────────────────┘
                         │
                         ▼
           ┌─────────────────────────┐
           │   Sensors Module        │
           │  (ADC + Pulse Counter)  │
           └────────────┬────────────┘
                        │
                        ▼
           ┌─────────────────────────┐
           │  Sample Collection      │
           │  (1s interval)          │
           └────────────┬────────────┘
                        │
                        ▼
           ┌─────────────────────────┐
           │   Protocol Buffers      │
           │  (Binary Serialization) │
           └────────────┬────────────┘
                        │
                        ▼
           ┌─────────────────────────┐
           │   MQTT Client           │
           │  (Secure Connection)    │
           └────────────┬────────────┘
                        │
                        ▼
               MQTT Broker (Backend)
```

## Build & Configuration

### Prerequisites
- **ESP-IDF**: v4.1.0 or higher
- **Python**: 3.6+
- **CMake**: 3.16+
- **ESP32 Development Board**

### Building the Project
```bash
idf.py build
```

### Flashing to ESP32
```bash
idf.py flash monitor
```

## Dependencies

- **nanopb**: Protocol Buffer encoder/decoder

## Device Identification

Each device gets a **unique ID** derived from its MAC address:
```c
app_device_id_init();
const char *device_id = app_get_device_id();
```

This ID is used for device identification in the backend system.

## Getting Started

1. Clone the repository and navigate to the ESP32 directory
2. Configure the project:
   ```bash
   idf.py menuconfig
   ```
3. Set MQTT broker credentials
4. Build and flash:
   ```bash
   idf.py build
   idf.py flash monitor
   ```

## Troubleshooting

- **WiFi Connection Issues**: Use BLE provisioning to reconfigure network settings. Press the reset button on the esp32 to reset the wifi credentials
- **Sensor Reading Errors**: Check GPIO connections and sensor calibration in `sensors.h`
- **MQTT Connection Failed**: Verify broker address and credentials in configuration

## License
Part of the Leak Detection System project.
