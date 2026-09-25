/*
Purpose: Implementation of the two physical buttons (see Buttons.h).
*/
#include "adapters/Buttons.h"
#include "core/Settings.h"
#include "config/HardwareConfiguration.h"

// A pin below zero is a button this board does not have: the external pair on
// most boards, and button B itself on the Waveshare, whose only free button is
// BOOT. pinMode(-1) is not a no-op, it indexes a GPIO table out of range, and
// digitalRead(-1) samples whatever that index lands on -- a phantom press
// waiting to happen. So every pin goes through these two, onboard ones too.
static void armButton(int8_t pin)
{
    if (pin >= 0)
        pinMode(pin, INPUT_PULLUP);
}

static bool buttonDown(int8_t pin)
{
    return pin >= 0 && digitalRead(pin) == LOW;
}

void Buttons::begin()
{
    _ready = g_settings.buttonsEnabled;
    if (!_ready)
        return;
    // INPUT_PULLUP + button to GND: released reads HIGH, pressed reads LOW.
    armButton(HardwareConfiguration::BUTTON_A_PIN);
    armButton(HardwareConfiguration::BUTTON_B_PIN);

    // The optional EXTERNAL pair, in parallel with the onboard one.
    armButton(HardwareConfiguration::BUTTON_A_EXT_PIN);
    armButton(HardwareConfiguration::BUTTON_B_EXT_PIN);

    if (HardwareConfiguration::BUTTON_A_EXT_PIN >= 0 ||
        HardwareConfiguration::BUTTON_B_EXT_PIN >= 0)
        Serial.printf("[buttons] enabled on GPIO %d (A) and %d (B), "
                      "external %d (A) and %d (B)\n",
                      (int)HardwareConfiguration::BUTTON_A_PIN,
                      (int)HardwareConfiguration::BUTTON_B_PIN,
                      (int)HardwareConfiguration::BUTTON_A_EXT_PIN,
                      (int)HardwareConfiguration::BUTTON_B_EXT_PIN);
    else if (HardwareConfiguration::BUTTON_B_PIN < 0)
        Serial.printf("[buttons] enabled on GPIO %d (A); this board has no button B\n",
                      (int)HardwareConfiguration::BUTTON_A_PIN);
    else
        Serial.printf("[buttons] enabled on GPIO %d (A) and %d (B)\n",
                      (int)HardwareConfiguration::BUTTON_A_PIN,
                      (int)HardwareConfiguration::BUTTON_B_PIN);
}

ButtonEvents Buttons::poll(unsigned long nowMs)
{
    ButtonEvents out;
    if (!_ready)
        return out;

    // OR the pairs together: either switch pressing is the action pressed. The
    // debounce/ramp state machine below then sees ONE logical button, so an
    // external press behaves identically to an onboard one -- including long
    // holds -- and holding both is the same as holding either. An absent pin
    // is never read (see buttonDown above).
    const bool aDown = buttonDown(HardwareConfiguration::BUTTON_A_PIN) ||
                       buttonDown(HardwareConfiguration::BUTTON_A_EXT_PIN);
    const bool bDown = buttonDown(HardwareConfiguration::BUTTON_B_PIN) ||
                       buttonDown(HardwareConfiguration::BUTTON_B_EXT_PIN);

    const ButtonState::Event ea = _a.update(aDown, nowMs);
    const ButtonState::Event eb = _b.update(bDown, nowMs);

    out.clickA = (ea == ButtonState::Event::Click);
    out.rampA = (ea == ButtonState::Event::Ramp);
    out.clickB = (eb == ButtonState::Event::Click);
    out.rampB = (eb == ButtonState::Event::Ramp);
    return out;
}
