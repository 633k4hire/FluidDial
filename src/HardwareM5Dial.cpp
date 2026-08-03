// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for the Arduino framework

#include "System.h"
#include "M5GFX.h"
#include "Drawing.h"
#include "HardwareM5Dial.hpp"
#ifdef USE_WIFI
#    include "WiFiConnection.h"
#endif

LGFX_Device&       display = M5Dial.Display;
LGFX_Sprite        canvas(&M5Dial.Display);
m5::Speaker_Class& speaker   = M5Dial.Speaker;
m5::Touch_Class&   touch     = M5Dial.Touch;
Stream&            debugPort = USBSerial;

m5::Button_Class& dialButton = M5Dial.BtnA;
m5::Button_Class  greenButton;
m5::Button_Class  redButton;

namespace {
struct SwitchEvent {
    bool    pressed;
    uint8_t button;
};

// M5Dial button state is edge-triggered. Controller command waits can keep the
// scene loop busy long enough for a short press and release to occur between
// dispatches, so retain those edges until dispatch_events() consumes them.
constexpr uint8_t SWITCH_EVENT_QUEUE_SIZE = 32;
SwitchEvent       switch_events[SWITCH_EVENT_QUEUE_SIZE];
uint8_t           switch_event_head = 0;
uint8_t           switch_event_tail = 0;

void queue_switch_event(bool pressed, uint8_t button) {
    uint8_t next = (switch_event_head + 1) % SWITCH_EVENT_QUEUE_SIZE;
    if (next == switch_event_tail) {
        return;
    }
    switch_events[switch_event_head] = { pressed, button };
    switch_event_head                = next;
}

void capture_switch_events(m5::Button_Class& source, uint8_t button) {
    if (source.wasPressed()) {
        queue_switch_event(true, button);
    }
    if (source.wasReleased()) {
        queue_switch_event(false, button);
    }
}
}  // namespace

bool round_display = true;

void init_hardware() {
    auto cfg = M5.config();

    // Don't enable the encoder because M5's encoder driver is flaky
    M5Dial.begin(cfg, false, false);

    // Turn on the power hold pin
    lgfx::gpio::command(lgfx::gpio::command_mode_output, GPIO_NUM_46);
    lgfx::gpio::command(lgfx::gpio::command_write_high, GPIO_NUM_46);

    // This must be done after M5Dial.begin which sets the PortA pins
    // to I2C mode.  We need to override that to use them for serial.
    // The baud rate is irrelevant because USBSerial emulates a UART
    // API but the data never travels over an actual physical UART
    // link with a defined baud rate.  The data instead travels over
    // a USB link at the USB data rate.  You can set the baud rate
    // at the other end to anything you want and it will still work.
    USBSerial.begin();

#ifdef USE_WIFI
    if (wifi_use_uart_mode()) {
        init_fnc_uart(FNC_UART_NUM, PND_TX_FNC_RX_PIN, PND_RX_FNC_TX_PIN);
    }
#else
    init_fnc_uart(FNC_UART_NUM, PND_TX_FNC_RX_PIN, PND_RX_FNC_TX_PIN);
#endif

    // Setup external GPIOs as buttons
    lgfx::gpio::command(lgfx::gpio::command_mode_input_pullup, RED_BUTTON_PIN);
    lgfx::gpio::command(lgfx::gpio::command_mode_input_pullup, GREEN_BUTTON_PIN);

    greenButton.setDebounceThresh(5);
    redButton.setDebounceThresh(5);

    init_encoder(ENC_A, ENC_B);

    speaker.setVolume(255);

    touch.setFlickThresh(30);
}

void reinit_fnc_uart() {
    init_fnc_uart(FNC_UART_NUM, PND_TX_FNC_RX_PIN, PND_RX_FNC_TX_PIN);
}

Point sprite_offset { 0, 0 };

extern const char* git_info;

void show_logo() {
    display.drawPngFile(LittleFS, "/fluid_dial.png", 0, 0, display.width(), display.height(), 0, 0, 0.0f, 0.0f, datum_t::middle_center);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.setTextDatum(datum_t::middle_center);
    display.setTextColor(TFT_DARKGREY);
    display.drawString(git_info, display.width() / 2, display.height() - 40);
}

void base_display() {
    display.clear();
}

void next_layout(int delta) {}

void system_background() {
    canvas.fillSprite(TFT_BLACK);
}

bool switch_button_touched(bool& pressed, int& button) {
    if (switch_event_tail == switch_event_head) {
        return false;
    }
    const SwitchEvent& event = switch_events[switch_event_tail];
    pressed                 = event.pressed;
    button                  = event.button;
    switch_event_tail       = (switch_event_tail + 1) % SWITCH_EVENT_QUEUE_SIZE;
    return true;
}

bool screen_encoder(int x, int y, int& delta) {
    return false;
}
bool screen_button_touched(bool pressed, int x, int y, int& button) {
    return false;
}

void update_events() {
    M5Dial.update();

    auto ms = m5gfx::millis();

    // The red and green buttons are active low
    redButton.setRawState(ms, !m5gfx::gpio_in(RED_BUTTON_PIN));
    greenButton.setRawState(ms, !m5gfx::gpio_in(GREEN_BUTTON_PIN));

    capture_switch_events(redButton, 0);
    capture_switch_events(dialButton, 1);
    capture_switch_events(greenButton, 2);
}

void ackBeep() {
    speaker.tone(1800, 50);
}

bool ui_locked(bool redrawButtonsFlag) {
    return false;
}

int num_layouts = 1;
int32_t layout_num = 0;
void redrawButtons() {}

int battery_level() {
    return -1;  // M5 Dial does not expose battery measuring circuitry
}

bool battery_charging() {
    return false;
}

#include <driver/rtc_io.h>
// The M5 Library is broken with respect to deep sleep on M5 Dial
// so we have to do it ourselves.  The problem is that the WAKE
// button is supposed to be the dial button that connects to GPIO42,
// but that can't work because GPIO42 is not an RTC GPIO and thus
// cannot be used as an ext0 wakeup source.
void deep_sleep(int us) {
    display.sleep();

    rtc_gpio_pullup_en((gpio_num_t)WAKEUP_GPIO);

    esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKEUP_GPIO, false);
    while (digitalRead(WAKEUP_GPIO) == false) {
        delay_ms(10);
    }
    if (us > 0) {
        esp_sleep_enable_timer_wakeup(us);
    } else {
        // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }
    esp_deep_sleep_start();
}
