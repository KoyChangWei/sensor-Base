# ESP32 IoT Manager

## Overview
This project implements an IoT device management system using ESP32 microcontrollers with Firebase integration and OLED display capabilities. It provides a web interface for remote device monitoring and configuration, along with a local access point mode for direct WiFi configuration.

## Features
- **Remote Device Monitoring**: View device status, messages, and identifiers through Firebase
- **Remote Configuration**: Update device settings without physical access
- **OLED Display**: Real-time status and message display on connected OLED
- **WiFi Management**: Easy configuration via Access Point mode
- **Remote WiFi Reconfiguration**: Trigger AP mode remotely via Firebase
- **Persistent Storage**: Credentials stored in EEPROM
- **Responsive Web Interface**: Mobile-friendly configuration portal

## Hardware Requirements
- ESP32 development board
- SSD1306 OLED display (128x32 or 128x64)
- Push button (connected to GPIO 0)
- Status LED (connected to GPIO 2)
- Power supply

## Software Requirements
- Arduino IDE
- Required Libraries:
  - EEPROM
  - WiFi
  - WebServer
  - HTTPClient
  - Wire
  - Adafruit_GFX
  - Adafruit_SSD1306
  - Firebase_ESP_Client

## Installation

### ESP32 Setup
1. Clone this repository
2. Open `main.ino` in Arduino IDE
3. Install all required libraries through the Library Manager
4. Configure your Firebase credentials in the code:
   ```cpp
   #define API_KEY "your-api-key"
   #define DATABASE_URL "your-database-url"
   #define USER_EMAIL "your-email"
   #define USER_PASSWORD "your-password"
   ```
5. Connect your ESP32 and upload the code

### Firebase Setup
1. Create a Firebase project at [Firebase Console](https://console.firebase.google.com/)
2. Set up Realtime Database with the following structure:
   ```
   /101/OLED/status
   /101/OLED/message
   /101/OLED/device_id
   /101/Credential/status
   ```
3. Configure Authentication with Email/Password provider
4. Add the user credentials matching those in your code

## Usage

### Normal Operation
1. On first boot, the device enters AP mode
2. Connect to the "ESP32_Config" WiFi network (password: "password")
3. Access the configuration portal at http://192.168.4.1
4. Enter your WiFi credentials and device ID
5. The device will restart and connect to your WiFi network
6. Monitor and control via Firebase and the OLED display

### Remote Configuration
1. Set `/101/Credential/status` to "0" in Firebase to trigger AP mode remotely
2. Use the configuration portal to update settings

### Manual Reconfiguration
Press the boot button (GPIO 0) to enter AP mode for reconfiguration

## Code Structure
- **WiFi Management**: Connection handling, AP mode, and web server
- **EEPROM Handling**: Persistent storage for credentials
- **OLED Control**: Display management functions
- **Firebase Integration**: Data fetching and updating
- **Main Loop**: Status monitoring and display updates

## Web Interface
The project includes a simple web application for device management. Use the provided HTML files:
- `index.html`: Landing page
- `login.html`: Authentication page
- Connect to your Firebase project for full functionality

## Troubleshooting
- **WiFi Connection Issues**: 
  - Verify credentials are correct
  - Check if router supports ESP32 connection
  - Press boot button to enter AP mode and reconfigure
  
- **Firebase Connection Issues**:
  - Verify API keys and credentials
  - Check Firebase rules to ensure read/write permissions
  - Monitor serial output for detailed error messages

- **OLED Not Working**:
  - Verify I2C connections (default: SDA=21, SCL=22)
  - Check OLED address (default: 0x3C)

## License
This project is licensed under the MIT License - see the LICENSE file for details. 