#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/time.h"
#include "esphome/components/display/display.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace vk16d32 {

class VK16D32Display;
using vk16d32_writer_t = display::DisplayWriter<VK16D32Display>;

// VK16D32: I²C LED matrix / 7-segment display driver
//   - 8 SEG outputs × 12 GRID outputs = 96 LEDs
//   - I²C address: 0x42

class VK16D32Display : public PollingComponent, public i2c::I2CDevice {
 public:
  void set_writer(vk16d32_writer_t &&writer) { this->writer_ = writer; }
  // brightness: 0 (min, ~4.4 mA) … 7 (max, 35 mA)
  void set_brightness(uint8_t brightness) {
    if (this->brightness_ != brightness) {
      this->brightness_ = brightness;
      this->brightness_changed_ = true;
    }
  }
  // num_grids: number of active GRID outputs (1–12)
  void set_num_grids(uint8_t num_grids) { this->num_grids_ = num_grids; }

  void setup() override;
  void dump_config() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  /// Write a raw segment byte directly to position `pos` in the buffer.
  /// Bits: dp=7, g=6, f=5, e=4, d=3, c=2, b=1, a=0.
  void set_segment_raw(uint8_t pos, uint8_t byte);

  /// Bulk-write raw bytes into the buffer from position 0. `len` is clamped to num_grids.
  void set_buffer(const uint8_t *data, uint8_t len);

  /// Print `str` starting at digit position `pos`.
  uint8_t print(uint8_t pos, const char *str);
  /// Print `str` starting at digit position 0.
  uint8_t print(const char *str);

  /// Evaluate printf-format and print at position `pos`.
  uint8_t printf(uint8_t pos, const char *format, ...) __attribute__((format(printf, 3, 4)));
  /// Evaluate printf-format and print at position 0.
  uint8_t printf(const char *format, ...) __attribute__((format(printf, 2, 3)));

  /// Evaluate strftime-format and print at position `pos`.
  uint8_t strftime(uint8_t pos, const char *format, ESPTime time) __attribute__((format(strftime, 3, 0)));
  /// Evaluate strftime-format and print at position 0.
  uint8_t strftime(const char *format, ESPTime time) __attribute__((format(strftime, 2, 0)));

  /// Push the current buffer to the display hardware.
  void display();

 protected:
  uint8_t brightness_{7};          // 0–7
  bool brightness_changed_{true};  // true on first call to display()
  uint8_t num_grids_{12};          // 1–12 active GRID lines
  uint8_t buffer_[12]{};           // one byte per GRID: SEG0=bit0 (a) … SEG6=bit6 (g), SEG7=bit7 (dp)
  vk16d32_writer_t writer_{};
};

}  // namespace vk16d32
}  // namespace esphome
