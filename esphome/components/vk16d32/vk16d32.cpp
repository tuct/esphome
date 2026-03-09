#include "vk16d32.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace vk16d32 {

static const char *const TAG = "display.vk16d32";

// ── Register addresses ────────────────────────────────────────────────────────
static constexpr uint8_t VK16D32_REG_RAM = 0x00;         // Display RAM base (0x00–0x0B)
static constexpr uint8_t VK16D32_REG_BRIGHTNESS = 0x10;  // Brightness level register
static constexpr uint8_t VK16D32_REG_SCAN = 0x11;        // Scan GRID count register
static constexpr uint8_t VK16D32_REG_STATE = 0x12;       // Shutdown / display-enable register

// State register data byte bits
static constexpr uint8_t VK16D32_STATE_NORMAL = 0x01;      // Bit 0: exit shutdown → normal mode
static constexpr uint8_t VK16D32_STATE_DISPLAY_ON = 0x02;  // Bit 1: enable display output

// Brightness data: value is the level directly (0–7), no base offset.
//   0x00 → ~4.4 mA (minimum)  …  0x07 → 35 mA (maximum)

// Scan data: value is num_grids - 1 (0–11), no base offset.
//   0x00 → 1 GRID active, 0x0B → 12 GRIDs active

// ── 7-segment ASCII lookup table ─────────────────────────────────────────────
// Bit layout matches standard common-cathode wiring to SEG0–SEG7:
//   bit0 = a (top)         bit4 = e (bottom-left)
//   bit1 = b (top-right)   bit5 = f (top-left)
//   bit2 = c (bottom-right)bit6 = g (middle)
//   bit3 = d (bottom)      bit7 = dp (decimal point)
//
//       a
//      ---
//   f |   | b
//      -g-
//   e |   | c
//      ---
//       d    dp
//
// Table starts at ASCII 0x20 (' ').
static constexpr uint8_t VK16D32_UNKNOWN_CHAR = 0xFF;
constexpr uint8_t VK16D32_ASCII_TO_RAW[] PROGMEM = {
    0b00000000,            // ' ', 0x20
    0b10000110,            // '!', 0x21
    0b00100010,            // '"', 0x22
    VK16D32_UNKNOWN_CHAR,  // '#', 0x23
    VK16D32_UNKNOWN_CHAR,  // '$', 0x24
    0b01001001,            // '%', 0x25
    VK16D32_UNKNOWN_CHAR,  // '&', 0x26
    0b00000010,            // ''', 0x27
    0b00111001,            // '(', 0x28
    0b00001111,            // ')', 0x29
    VK16D32_UNKNOWN_CHAR,  // '*', 0x2A
    VK16D32_UNKNOWN_CHAR,  // '+', 0x2B
    0b00001000,            // ',', 0x2C
    0b01000000,            // '-', 0x2D
    0b10000000,            // '.', 0x2E
    VK16D32_UNKNOWN_CHAR,  // '/', 0x2F
    0b00111111,            // '0', 0x30
    0b00000110,            // '1', 0x31
    0b01011011,            // '2', 0x32
    0b01001111,            // '3', 0x33
    0b01100110,            // '4', 0x34
    0b01101101,            // '5', 0x35
    0b01111101,            // '6', 0x36
    0b00000111,            // '7', 0x37
    0b01111111,            // '8', 0x38
    0b01101111,            // '9', 0x39
    0b00001001,            // ':', 0x3A  (top + bottom segments)
    0b01001000,            // ';', 0x3B
    VK16D32_UNKNOWN_CHAR,  // '<', 0x3C
    0b01001000,            // '=', 0x3D
    VK16D32_UNKNOWN_CHAR,  // '>', 0x3E
    0b01010011,            // '?', 0x3F
    0b01011111,            // '@', 0x40
    0b01110111,            // 'A', 0x41
    0b01111100,            // 'B', 0x42  (lowercase b shape)
    0b00111001,            // 'C', 0x43
    0b01011110,            // 'D', 0x44  (lowercase d shape)
    0b01111001,            // 'E', 0x45
    0b01110001,            // 'F', 0x46
    0b00111101,            // 'G', 0x47
    0b01110110,            // 'H', 0x48
    0b00000110,            // 'I', 0x49
    0b00011110,            // 'J', 0x4A
    VK16D32_UNKNOWN_CHAR,  // 'K', 0x4B
    0b00111000,            // 'L', 0x4C
    VK16D32_UNKNOWN_CHAR,  // 'M', 0x4D
    0b01010100,            // 'N', 0x4E  (lowercase n shape)
    0b00111111,            // 'O', 0x4F
    0b01110011,            // 'P', 0x50
    VK16D32_UNKNOWN_CHAR,  // 'Q', 0x51
    0b01010000,            // 'R', 0x52  (lowercase r shape)
    0b01101101,            // 'S', 0x53
    0b01111000,            // 'T', 0x54  (lowercase t shape)
    0b00111110,            // 'U', 0x55
    0b00111110,            // 'V', 0x56
    VK16D32_UNKNOWN_CHAR,  // 'W', 0x57
    VK16D32_UNKNOWN_CHAR,  // 'X', 0x58
    0b01101110,            // 'Y', 0x59
    0b01011011,            // 'Z', 0x5A
    0b00111001,            // '[', 0x5B
    VK16D32_UNKNOWN_CHAR,  // '\', 0x5C
    0b00001111,            // ']', 0x5D
    VK16D32_UNKNOWN_CHAR,  // '^', 0x5E
    0b00001000,            // '_', 0x5F
    VK16D32_UNKNOWN_CHAR,  // '`', 0x60
    0b01110111,            // 'a', 0x61
    0b01111100,            // 'b', 0x62
    0b01011000,            // 'c', 0x63
    0b01011110,            // 'd', 0x64
    0b01111001,            // 'e', 0x65
    0b01110001,            // 'f', 0x66
    0b01101111,            // 'g', 0x67
    0b01110100,            // 'h', 0x68
    0b00000100,            // 'i', 0x69
    0b00001110,            // 'j', 0x6A
    VK16D32_UNKNOWN_CHAR,  // 'k', 0x6B
    0b00110000,            // 'l', 0x6C
    VK16D32_UNKNOWN_CHAR,  // 'm', 0x6D
    0b01010100,            // 'n', 0x6E
    0b01011100,            // 'o', 0x6F
    0b01110011,            // 'p', 0x70
    VK16D32_UNKNOWN_CHAR,  // 'q', 0x71
    0b01010000,            // 'r', 0x72
    0b01101101,            // 's', 0x73
    0b01111000,            // 't', 0x74
    0b00011100,            // 'u', 0x75
    0b00011100,            // 'v', 0x76
    VK16D32_UNKNOWN_CHAR,  // 'w', 0x77
    VK16D32_UNKNOWN_CHAR,  // 'x', 0x78
    0b01101110,            // 'y', 0x79
    VK16D32_UNKNOWN_CHAR,  // 'z', 0x7A
    0b00111001,            // '{', 0x7B
    0b00000110,            // '|', 0x7C
    0b00001111,            // '}', 0x7D
    0b01100011,            // '~', 0x7E  (degree symbol ° approximation)
};

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void VK16D32Display::setup() {
  ESP_LOGCONFIG(TAG, "Setting up VK16D32...");

  // Enter normal mode first (display still off) — matches captured init sequence
  const uint8_t state_normal = VK16D32_STATE_NORMAL;
  if (!this->write_bytes(VK16D32_REG_STATE, &state_normal, 1)) {
    ESP_LOGE(TAG, "VK16D32 state init failed — check wiring and I²C address");
    this->mark_failed();
    return;
  }

  // Brightness: level 0–7 written directly
  const uint8_t brightness_data = this->brightness_ & 0x07;
  if (!this->write_bytes(VK16D32_REG_BRIGHTNESS, &brightness_data, 1)) {
    ESP_LOGE(TAG, "VK16D32 brightness init failed");
    this->mark_failed();
    return;
  }
  this->brightness_changed_ = false;

  // Scan: num_grids - 1 written directly (0x00 = 1 grid, 0x0B = 12 grids)
  const uint8_t scan = static_cast<uint8_t>(this->num_grids_ - 1);
  if (!this->write_bytes(VK16D32_REG_SCAN, &scan, 1)) {
    ESP_LOGE(TAG, "VK16D32 scan init failed");
    this->mark_failed();
    return;
  }

  // Clear RAM
  this->display();

  // Enable display output now that RAM is initialised
  const uint8_t state_on = VK16D32_STATE_NORMAL | VK16D32_STATE_DISPLAY_ON;
  if (!this->write_bytes(VK16D32_REG_STATE, &state_on, 1)) {
    ESP_LOGE(TAG, "VK16D32 display-on failed");
    this->mark_failed();
    return;
  }
}

void VK16D32Display::dump_config() {
  ESP_LOGCONFIG(TAG,
                "VK16D32:\n"
                "  Brightness: %u/7\n"
                "  Active GRIDs: %u",
                this->brightness_, this->num_grids_);
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
}

void VK16D32Display::update() {
  for (uint8_t &b : this->buffer_)
    b = 0;
  if (this->writer_.has_value())
    (*this->writer_)(*this);
  this->display();
}

// ── Hardware write ─────────────────────────────────────────────────────────────

void VK16D32Display::display() {
  // Brightness: level 0–7 written directly. Only on first call or when changed.
  if (this->brightness_changed_) {
    const uint8_t brightness_data = this->brightness_ & 0x07;
    this->write_bytes(VK16D32_REG_BRIGHTNESS, &brightness_data, 1);
    this->brightness_changed_ = false;
  }

  // Write display RAM: one byte per active GRID, auto-incremented from address 0x00
  this->write_bytes(VK16D32_REG_RAM, this->buffer_, this->num_grids_);
}

// ── Raw buffer access ──────────────────────────────────────────────────────────

void VK16D32Display::set_segment_raw(uint8_t pos, uint8_t byte) {
  if (pos >= this->num_grids_) {
    ESP_LOGE(TAG, "set_segment_raw: position %u out of range (num_grids=%u)", pos, this->num_grids_);
    return;
  }
  this->buffer_[pos] = byte;
}

void VK16D32Display::set_buffer(const uint8_t *data, uint8_t len) {
  const uint8_t count = std::min(len, this->num_grids_);
  for (uint8_t i = 0; i < count; i++)
    this->buffer_[i] = data[i];
}

// ── Text rendering ─────────────────────────────────────────────────────────────

uint8_t VK16D32Display::print(uint8_t start_pos, const char *str) {
  uint8_t pos = start_pos;
  for (; *str != '\0'; str++) {
    uint8_t data = VK16D32_UNKNOWN_CHAR;
    if (*str >= ' ' && *str <= '~')
      data = progmem_read_byte(&VK16D32_ASCII_TO_RAW[*str - ' ']);

    if (data == VK16D32_UNKNOWN_CHAR) {
      ESP_LOGW(TAG, "Character '%c' has no VK16D32 representation", *str);
    }

    if (*str == '.') {
      // Merge decimal point into the preceding digit's byte
      if (pos > start_pos)
        this->buffer_[pos - 1] |= 0b10000000;
      continue;
    }

    if (pos >= this->num_grids_) {
      ESP_LOGE(TAG, "String is too long for the display!");
      break;
    }
    this->buffer_[pos++] = data;
  }
  return pos - start_pos;
}

uint8_t VK16D32Display::print(const char *str) { return this->print(0, str); }

uint8_t VK16D32Display::printf(uint8_t pos, const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  char buffer[64];
  int ret = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);
  if (ret > 0)
    return this->print(pos, buffer);
  return 0;
}

uint8_t VK16D32Display::printf(const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  char buffer[64];
  int ret = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);
  if (ret > 0)
    return this->print(buffer);
  return 0;
}

uint8_t VK16D32Display::strftime(uint8_t pos, const char *format, ESPTime time) {
  char buffer[64];
  size_t ret = time.strftime(buffer, sizeof(buffer), format);
  if (ret > 0)
    return this->print(pos, buffer);
  return 0;
}

uint8_t VK16D32Display::strftime(const char *format, ESPTime time) { return this->strftime(0, format, time); }

}  // namespace vk16d32
}  // namespace esphome
