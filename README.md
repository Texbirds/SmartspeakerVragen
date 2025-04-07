# Smart Speaker with ChatGPT Integration (ESP32 + LyraT v4.3)

This project transforms your ESP32-based LyraT board into a voice-controlled smart speaker. It can:

- Record your voice on command
- Transcribe audio using **AssemblyAI**
- Ask ChatGPT your question
- Display the response on an **HD44780 4x20 LCD**
- Let you scroll through the response using buttons

---

## Requirements

- **ESP32 LyraT v4.3**
- **microSD card** (FAT32 formatted)
- **HD44780-compatible 4x20 LCD**
- **3 Buttons** (via GPIO or MCP23017)
- **Wi-Fi connection**

---

## DIP Switch Settings (LyraT v4.3)

Ensure the following switch configuration:

- **Only switch 7 should be ON**
- **All other switches (1–6, 8) OFF**

---

## Button Configuration

Connect buttons to the following pins:

| Function       | Pin Number |
|----------------|------------|
| Scroll Down    | GPIO 0     |
| Scroll Up      | GPIO 1     |
| Select / Start | GPIO 2     |

These can be connected directly to GPIOs or through an **MCP23017 I/O expander**.

---

## File Handling

- Voice recordings are saved as: `/sdcard/ask.wav`
- ChatGPT answers are shown on the LCD

---

## Wi-Fi Setup

Before flashing the firmware, update the credentials in your code:

```c
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASS "your-wifi-password"

## Extra

You need to define:
#define OPENAI_API_KEY   
#define API_KEY
In a header file named secrets.h