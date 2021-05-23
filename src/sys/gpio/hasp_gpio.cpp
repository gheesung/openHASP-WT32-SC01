/* MIT License - Copyright (c) 2019-2021 Francis Van Roie
   For full license information read the LICENSE file in the project folder */

#include "lv_conf.h" // For timing defines

#include "hasplib.h"

#include "hasp_gpio.h"
#include "hasp_config.h"

#ifdef ARDUINO_ARCH_ESP8266
#define INPUT_PULLDOWN INPUT
#endif

#ifdef ARDUINO
#include "AceButton.h"

using namespace ace_button;
ButtonConfig buttonConfig; // Clicks, double-clicks and long presses
ButtonConfig switchConfig; // Clicks only
#else

#define HIGH 1
#define LOW 0
#define NUM_DIGITAL_PINS 40
#define digitalWrite(x, y)
#define analogWrite(x, y)

#endif // ARDUINO

#define SCALE_8BIT_TO_12BIT(x) x << 4 | x >> 4
#define SCALE_8BIT_TO_10BIT(x) x << 2 | x >> 6

// An array of button pins, led pins, and the led states. Cannot be const
// because ledState is mutable.
hasp_gpio_config_t gpioConfig[HASP_NUM_GPIO_CONFIG] = {
    //    {2, 8, INPUT, LOW}, {3, 9, OUTPUT, LOW}, {4, 10, INPUT, HIGH}, {5, 11, OUTPUT, LOW}, {6, 12, INPUT, LOW},
};
uint8_t pwm_channel = 1; // Backlight has 0

static inline void gpio_update_group(uint8_t group, lv_obj_t* obj, bool power, int32_t val, int32_t min, int32_t max)
{
    hasp_update_value_t value = {.obj = obj, .group = group, .min = min, .max = max, .val = val, .power = power};
    dispatch_normalized_group_values(value);
}

#if defined(ARDUINO_ARCH_ESP32)
#include "driver/uart.h"
#include <driver/dac.h>

volatile bool touchdetected        = false;
RTC_DATA_ATTR int rtcRecordCounter = 0;

void gotTouch()
{
    touchdetected = true;
}

// Overrides the readButton function on ESP32
class CapacitiveConfig : public ButtonConfig {

  protected:
    // Number of iterations to sample the capacitive switch. Higher number
    // provides better smoothing but increases the time taken for a single read.
    // static const uint8_t kSamples = 10;

    // The threshold value which is considered to be a "touch" on the switch.
    static const long kTouchThreshold = 32;

    int readButton(uint8_t pin) override
    {
        return touchdetected ? HIGH : LOW; // HIGH = not touched
    }
};
CapacitiveConfig touchConfig; // Capacitive touch
#endif

void gpio_log_serial_dimmer(const char* command)
{
    char buffer[32];
    snprintf_P(buffer, sizeof(buffer), PSTR("Dimmer => %02x %02x %02x %02x"), command[0], command[1], command[2],
               command[3]);
    LOG_VERBOSE(TAG_GPIO, buffer);
}

#ifdef ARDUINO
static void gpio_event_handler(AceButton* button, uint8_t eventType, uint8_t buttonState)
{
    uint8_t btnid = button->getId();
    uint8_t eventid;
    bool state = false;
    switch(eventType) {
        case AceButton::kEventPressed:
            if(gpioConfig[btnid].type != hasp_gpio_type_t::BUTTON) {
                eventid                 = HASP_EVENT_ON;
                gpioConfig[btnid].power = 1;
            } else {
                eventid = HASP_EVENT_DOWN;
            }
            state = true;
            // touchdetected = false;
            break;
        case 2: // AceButton::kEventClicked:
            eventid = HASP_EVENT_UP;
            break;
        // case AceButton::kEventDoubleClicked:
        //     eventid = HASP_EVENT_DOUBLE;
        //     break;
        case AceButton::kEventLongPressed:
            eventid = HASP_EVENT_LONG;
            // state = true; // do not repeat DOWN + LONG
            break;
        // case AceButton::kEventRepeatPressed:
        //     eventid = HASP_EVENT_HOLD;
        //     state = true; // do not repeat DOWN + LONG + HOLD
        //     break;
        case AceButton::kEventReleased:
            if(gpioConfig[btnid].type != hasp_gpio_type_t::BUTTON) {
                eventid                 = HASP_EVENT_OFF;
                gpioConfig[btnid].power = 0;
            } else {
                eventid = HASP_EVENT_RELEASE;
            }
            break;
        default:
            eventid = HASP_EVENT_LOST;
    }

    event_gpio_input(gpioConfig[btnid].pin, eventid);

    // update objects and gpios in this group
    if(gpioConfig[btnid].group && eventid != HASP_EVENT_LONG) // do not repeat DOWN + LONG
        gpio_update_group(gpioConfig[btnid].group, NULL, gpioConfig[btnid].power, state, HASP_EVENT_OFF, HASP_EVENT_ON);
}

/* ********************************* GPIO Setup *************************************** */

void aceButtonSetup(void)
{
    // Button Features
    buttonConfig.setEventHandler(gpio_event_handler);
    buttonConfig.setFeature(ButtonConfig::kFeatureClick);
    buttonConfig.clearFeature(ButtonConfig::kFeatureDoubleClick);
    buttonConfig.setFeature(ButtonConfig::kFeatureLongPress);
    // buttonConfig.clearFeature(ButtonConfig::kFeatureRepeatPress);
    buttonConfig.clearFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick); // Causes annoying pauses
    buttonConfig.setFeature(ButtonConfig::kFeatureSuppressAfterClick);
    // Delays
    buttonConfig.setClickDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    buttonConfig.setDoubleClickDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    buttonConfig.setLongPressDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    buttonConfig.setRepeatPressDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    buttonConfig.setRepeatPressInterval(LV_INDEV_DEF_LONG_PRESS_REP_TIME);

    // Switch Features
    switchConfig.setEventHandler(gpio_event_handler);
    switchConfig.setFeature(ButtonConfig::kFeatureClick);
    switchConfig.clearFeature(ButtonConfig::kFeatureLongPress);
    switchConfig.clearFeature(ButtonConfig::kFeatureRepeatPress);
    switchConfig.clearFeature(ButtonConfig::kFeatureDoubleClick);
    switchConfig.setClickDelay(100); // decrease click delay from default 200 ms

#if defined(ARDUINO_ARCH_ESP32)
    // Capacitive Touch Features
    touchConfig.setEventHandler(gpio_event_handler);
    touchConfig.setFeature(ButtonConfig::kFeatureClick);
    touchConfig.clearFeature(ButtonConfig::kFeatureDoubleClick);
    touchConfig.setFeature(ButtonConfig::kFeatureLongPress);
    // touchConfig.clearFeature(ButtonConfig::kFeatureRepeatPress);
    touchConfig.clearFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick); // Causes annoying pauses
    touchConfig.setFeature(ButtonConfig::kFeatureSuppressAfterClick);
    // Delays
    touchConfig.setClickDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    touchConfig.setDoubleClickDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    touchConfig.setLongPressDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    touchConfig.setRepeatPressDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    touchConfig.setRepeatPressInterval(LV_INDEV_DEF_LONG_PRESS_REP_TIME);
#endif
}

// Can be called ad-hoc to change a setup
static void gpio_setup_pin(uint8_t index)
{
    hasp_gpio_config_t* gpio = &gpioConfig[index];

    if(gpioIsSystemPin(gpio->pin)) {
        LOG_WARNING(TAG_GPIO, F("Invalid pin %d"), gpio->pin);
        return;
    }

    uint8_t input_mode;
    bool default_state = gpio->inverted ? LOW : HIGH; // default pullup
    switch(gpio->gpio_function) {
        case hasp_gpio_function_t::OUTPUT_PIN:
            input_mode = OUTPUT;
            break;
        case hasp_gpio_function_t::EXTERNAL_PULLDOWN:
            default_state = !default_state; // not pullup
        case hasp_gpio_function_t::EXTERNAL_PULLUP:
            input_mode = INPUT;
            break;
#ifndef ARDUINO_ARCH_ESP8266
        case hasp_gpio_function_t::INTERNAL_PULLDOWN:
            default_state = !default_state; // not pullup
            input_mode    = INPUT_PULLDOWN;
            break;
#endif
        case hasp_gpio_function_t::INTERNAL_PULLUP:
        default:
            input_mode = INPUT_PULLUP;
    }

    gpio->power = 1; // on by default, value is set to 0
    gpio->max   = 255;
    switch(gpio->type) {
        case hasp_gpio_type_t::SWITCH:
        case hasp_gpio_type_t::BATTERY... hasp_gpio_type_t::WINDOW:
            if(gpio->btn) delete gpio->btn;
            gpio->btn = new AceButton(&switchConfig, gpio->pin, default_state, index);
            pinMode(gpio->pin, INPUT_PULLUP);
            gpio->max = 0;
            break;
        case hasp_gpio_type_t::BUTTON:
            if(gpio->btn) delete gpio->btn;
            gpio->btn = new AceButton(&buttonConfig, gpio->pin, default_state, index);
            pinMode(gpio->pin, INPUT_PULLUP);
            gpio->max = 0;
            break;
#if defined(ARDUINO_ARCH_ESP32)
        case hasp_gpio_type_t::TOUCH:
            if(gpio->btn) delete gpio->btn;
            gpio->btn = new AceButton(&touchConfig, gpio->pin, HIGH, index);
            gpio->max = 0;
            // touchAttachInterrupt(gpio->pin, gotTouch, 33);
            break;
#endif

        case hasp_gpio_type_t::POWER_RELAY:
        case hasp_gpio_type_t::LIGHT_RELAY:
            pinMode(gpio->pin, OUTPUT);
            gpio->max = 1; // on-off
            break;

        case hasp_gpio_type_t::PWM:
            gpio->max = 4095;
        case hasp_gpio_type_t::LED... hasp_gpio_type_t::LED_W:
            // case hasp_gpio_type_t::BACKLIGHT:
            pinMode(gpio->pin, OUTPUT);
#if defined(ARDUINO_ARCH_ESP32)
            if(pwm_channel < 16) {
                // configure LED PWM functionalitites
                ledcSetup(pwm_channel, 20000, 12);
                // attach the channel to the GPIO to be controlled
                ledcAttachPin(gpio->pin, pwm_channel);
                gpio->channel = pwm_channel++;
            } else {
                LOG_ERROR(TAG_GPIO, F("Too many PWM channels defined"));
            }
#endif
            break;

        case hasp_gpio_type_t::DAC:
#if defined(ARDUINO_ARCH_ESP32)
            gpio_num_t pin;
            if(dac_pad_get_io_num(DAC_CHANNEL_1, &pin) == ESP_OK)
                if(gpio->pin == pin) dac_output_enable(DAC_CHANNEL_1);
            if(dac_pad_get_io_num(DAC_CHANNEL_2, &pin) == ESP_OK)
                if(gpio->pin == pin) dac_output_enable(DAC_CHANNEL_2);
#endif
            break;

        case hasp_gpio_type_t::SERIAL_DIMMER:
        case hasp_gpio_type_t::SERIAL_DIMMER_AU:
        case hasp_gpio_type_t::SERIAL_DIMMER_EU: {
            const char command[9] = "\xEF\x01\x4D\xA3"; // Start Lanbon Dimmer
#if defined(ARDUINO_ARCH_ESP32)
            Serial1.begin(115200UL, SERIAL_8N1, UART_PIN_NO_CHANGE, gpio->pin,
                          gpio->type == hasp_gpio_type_t::SERIAL_DIMMER_EU); // true = EU, false = AU
            Serial1.flush();
            delay(20);
            Serial1.print("  ");
            delay(20);
            Serial1.write((const uint8_t*)command, 8);
#endif
            gpio_log_serial_dimmer(command);
            break;
        }

        case hasp_gpio_type_t::FREE:
            return;

        default:
            LOG_WARNING(TAG_GPIO, F("Invalid config -> pin %d - type: %d"), gpio->pin, gpio->type);
    }
    LOG_VERBOSE(TAG_GPIO, F(D_BULLET "Configured pin %d"), gpio->pin);
}

void gpioSetup()
{
    LOG_INFO(TAG_GPIO, F(D_SERVICE_STARTING));
#if defined(ARDUINO_ARCH_ESP32)
    LOG_WARNING(TAG_GPIO, F("Reboot counter %d"), rtcRecordCounter++);
#endif

    aceButtonSetup();

    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        gpio_setup_pin(i);
    }

    LOG_INFO(TAG_GPIO, F(D_SERVICE_STARTED));
}

IRAM_ATTR void gpioLoop(void)
{
    // Should be called every 4-5ms or faster, for the default debouncing time of ~20ms.
    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        if(gpioConfig[i].btn) gpioConfig[i].btn->check();
    }
}

#else

void gpioSetup(void)
{
    gpioSavePinConfig(0, 3, hasp_gpio_type_t::POWER_RELAY, 0, -1, false);
    gpioSavePinConfig(1, 4, hasp_gpio_type_t::LIGHT_RELAY, 0, -1, false);
    gpioSavePinConfig(2, 13, hasp_gpio_type_t::LED, 0, -1, false);
    gpioConfig[2].max = 255;
    gpioSavePinConfig(3, 14, hasp_gpio_type_t::DAC, 0, -1, false);
    gpioConfig[2].max = 4095;
    gpioSavePinConfig(4, 5, hasp_gpio_type_t::MOTION, 0, -1, false);
}
IRAM_ATTR void gpioLoop(void)
{}

#endif // ARDUINO

static inline bool gpio_is_input(hasp_gpio_config_t* gpio)
{
    return (gpio->type != hasp_gpio_type_t::USER) && (gpio->type >= 0x80);
}

static inline bool gpio_is_output(hasp_gpio_config_t* gpio)
{
    return (gpio->type > hasp_gpio_type_t::USED) && (gpio->type < 0x80);
}

void gpioEvery5Seconds(void)
{
    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        if(gpio_is_input(&gpioConfig[i])) {
            gpioConfig[i].power = !gpioConfig[i].power;
            event_gpio_input(gpioConfig[i].pin, gpioConfig[i].power);
        }
    }
}

/* ********************************* State Setters *************************************** */

bool gpio_get_pin_state(uint8_t pin, bool& power, int32_t& val)
{
    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        if(gpioConfig[i].pin == pin && gpio_is_output(&gpioConfig[i])) {
            power = gpioConfig[i].power;
            val   = gpioConfig[i].val;
            return true;
        }
    }
    return false;
}

static inline void gpio_input_state(hasp_gpio_config_t* gpio)
{
    event_gpio_input(gpio->pin, gpio->power);
}

void gpio_output_state(hasp_gpio_config_t* gpio)
{
    char payload[32];
    char topic[12];
    snprintf_P(topic, sizeof(topic), PSTR("output%d"), gpio->pin);
    snprintf_P(payload, sizeof(payload), PSTR("{\"state\":%d,\"val\":%d}"), gpio->power, gpio->val);

    dispatch_state_subtopic(topic, payload);
}

bool gpio_input_pin_state(uint8_t pin)
{
    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        if(gpioConfig[i].pin == pin && gpioConfigInUse(i)) {
            gpio_input_state(&gpioConfig[i]);
            return true;
        }
    }
    return false;
}

bool gpio_output_pin_state(uint8_t pin)
{
    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        if(gpioConfig[i].pin == pin && gpioConfigInUse(i)) {
            gpio_output_state(&gpioConfig[i]);
            return true;
        }
    }
    return false;
}

static inline int32_t gpio_limit(int32_t val, int32_t min, int32_t max)
{
    if(val >= max) return max;
    if(val <= min) return min;
    return val;
}

// val is assumed to be 12 bits
static inline bool gpio_set_analog_value(hasp_gpio_config_t* gpio)
{
    uint16_t val = 0;
#if defined(ARDUINO_ARCH_ESP32)

    if(gpio->max == 255)
        val = SCALE_8BIT_TO_12BIT(gpio->val);
    else if(gpio->max == 4095)
        val = gpio->val;

    if(!gpio->power) val = 0;
    if(gpio->inverted) val = 4095 - val;

    ledcWrite(gpio->channel, val); // 12 bits
    return true;                   // sent

#elif defined(ARDUINO_ARCH_ESP8266)

    if(gpio->max == 255)
        val = SCALE_8BIT_TO_10BIT(gpio->val);
    else if(gpio->max == 4095)
        val = gpio->val >> 2;

    if(!gpio->power) val = 0;
    if(gpio->inverted) val = 1023 - val;

    analogWrite(gpio->pin, val); // 10 bits
    return true;                 // sent

#else
    return false; // not implemented
#endif
}

static inline bool gpio_set_serial_dimmer(hasp_gpio_config_t* gpio)
{
    uint16_t val = gpio_limit(gpio->val, 0, 255);

    if(!gpio->power) val = 0;
    if(gpio->inverted) val = 255 - val;

    char command[5] = "\xEF\x02\x00\xED";
    command[2]      = (uint8_t)map(val, 0, 255, 0, 100);
    command[3] ^= command[2];

#if defined(ARDUINO_ARCH_ESP32)
    Serial1.write((const uint8_t*)command, 4);
    gpio_log_serial_dimmer(command);
    return true; // sent
#else
    gpio_log_serial_dimmer(command);
    return false; // not sent
#endif
}

static inline bool gpio_set_dac_value(hasp_gpio_config_t* gpio)
{
#ifdef ARDUINO_ARCH_ESP32
    uint16_t val = gpio_limit(gpio->val, 0, 255);
    gpio_num_t pin;

    if(!gpio->power) val = 0;
    if(gpio->inverted) val = 255 - val;

    if(dac_pad_get_io_num(DAC_CHANNEL_1, &pin) == ESP_OK && gpio->pin == pin)
        dac_output_voltage(DAC_CHANNEL_1, gpio->val);
    else if(dac_pad_get_io_num(DAC_CHANNEL_2, &pin) == ESP_OK && gpio->pin == pin)
        dac_output_voltage(DAC_CHANNEL_2, gpio->val);
    else
        return false; // not found
    return true;      // found
#else
    return false; // not implemented
#endif
}

bool gpio_get_pin_config(uint8_t pin, hasp_gpio_config_t** gpio)
{
    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        if(gpioConfig[i].pin == pin) {
            *gpio = &gpioConfig[i];
            return true;
        }
    }
    return false;
}

// Update the actual value of one pin, does NOT update group members
// The value must be normalized first
static bool gpio_set_output_value(hasp_gpio_config_t* gpio, bool power, uint16_t val)
{
    // if val is 0, then set power to 0
    gpio->power = val == 0 ? 0 : power;

    // Only update the current value if power set to 1, otherwise retain previous value
    if(val != 0) gpio->val = gpio_limit(val, 0, gpio->max);

    switch(gpio->type) {
        case hasp_gpio_type_t::POWER_RELAY:
        case hasp_gpio_type_t::LIGHT_RELAY:
            digitalWrite(gpio->pin, power ? (gpio->inverted ? !gpio->val : gpio->val) : 0);
            return true;

        case hasp_gpio_type_t::LED... hasp_gpio_type_t::LED_W:
        case hasp_gpio_type_t::PWM:
            return gpio_set_analog_value(gpio);

        case hasp_gpio_type_t::DAC:
            return gpio_set_dac_value(gpio);

        case hasp_gpio_type_t::SERIAL_DIMMER:
        case hasp_gpio_type_t::SERIAL_DIMMER_AU:
        case hasp_gpio_type_t::SERIAL_DIMMER_EU:
            return gpio_set_serial_dimmer(gpio);

        default:
            LOG_WARNING(TAG_GPIO, F(D_BULLET "Pin %d is not a valid output"), gpio->pin);
            return false; // not a valid output
    }
}

// Update the normalized value of one pin
static void gpio_set_normalized_value(hasp_gpio_config_t* gpio, hasp_update_value_t& value)
{
    int32_t val = value.val;

    if(value.min != 0 || value.max != gpio->max) { // do we need to recalculate?
        if(value.min == value.max) {
            LOG_ERROR(TAG_GPIO, F("Invalid value range"));
            return;
        }

        switch(gpio->type) {
            case hasp_gpio_type_t::POWER_RELAY:
            case hasp_gpio_type_t::LIGHT_RELAY:
                val = val > value.min ? HIGH : LOW;
                break;

            case hasp_gpio_type_t::LED... hasp_gpio_type_t::LED_W:
            case hasp_gpio_type_t::DAC:
            case hasp_gpio_type_t::PWM:
            case hasp_gpio_type_t::SERIAL_DIMMER:
            case hasp_gpio_type_t::SERIAL_DIMMER_AU:
            case hasp_gpio_type_t::SERIAL_DIMMER_EU:
                val = map(val, value.min, value.max, 0, gpio->max);
                break;

            default:
                return; // invalid output type
        }
    }

    gpio_set_output_value(gpio, value.power, val); // recalculated
}

// Dispatch all group member values
void gpio_output_group_values(uint8_t group)
{
    for(uint8_t k = 0; k < HASP_NUM_GPIO_CONFIG; k++) {
        hasp_gpio_config_t* gpio = &gpioConfig[k];
        if(gpio->group == group && gpio_is_output(gpio)) // group members that are outputs
            gpio_output_state(&gpioConfig[k]);
    }
}

// SHOULD only by called from DISPATCH
// Update the normalized value of all group members
// Does not procude logging output
void gpio_set_normalized_group_values(hasp_update_value_t& value)
{
    // Set all pins first, minimizes delays
    for(uint8_t k = 0; k < HASP_NUM_GPIO_CONFIG; k++) {
        hasp_gpio_config_t* gpio = &gpioConfig[k];
        if(gpio->group == value.group && gpioConfigInUse(k)) // group members that are outputs
            gpio_set_normalized_value(gpio, value);
    }

    // Log the changed output values
    // gpio_output_group_values(value.group);

    // object_set_normalized_group_values(group, NULL, val, min, max); // Update onsreen objects
}

// Update the value of an output pin and its group members
bool gpio_set_pin_state(uint8_t pin, bool power, int32_t val)
{
    hasp_gpio_config_t* gpio = NULL;

    if(!gpio_get_pin_config(pin, &gpio) || !gpio) {
        LOG_WARNING(TAG_GPIO, F(D_BULLET "Pin %d is not configured"), pin);
        return false;
    }

    if(!gpio_is_output(gpio)) {
        LOG_WARNING(TAG_GPIO, F(D_BULLET "Pin %d can not be set"), pin);
        return false;
    }

    if(gpio->group) {
        // update objects and gpios in this group
        gpio->power = power;
        gpio->val   = gpio_limit(val, 0, gpio->max);
        gpio_update_group(gpio->group, NULL, gpio->power, gpio->val, 0, gpio->max);

    } else {
        // update this gpio value only
        if(gpio_set_output_value(gpio, power, val)) {
            gpio_output_state(gpio);
            LOG_VERBOSE(TAG_GPIO, F("No Group - Pin %d = %d"), gpio->pin, gpio->val);
        } else {
            return false;
        }
    }

    return true; // pin found and set
}

// Updates the RGB pins directly, rgb are already normalized values
void gpio_set_moodlight(moodlight_t& moodlight)
{
    // RGBXX https://stackoverflow.com/questions/39949331/how-to-calculate-rgbaw-amber-white-from-rgb-for-leds
    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        switch(gpioConfig[i].type) {
            case hasp_gpio_type_t::LED_R... hasp_gpio_type_t::LED_W:
                uint8_t index = (gpioConfig[i].type - hasp_gpio_type_t::LED_R);
                if(index > 4) continue;

                uint8_t val = (moodlight.rgbww[index] * moodlight.brightness + 127) / 255;
                gpio_set_output_value(&gpioConfig[i], moodlight.power, val);
                break;
        }
    }

    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        switch(gpioConfig[i].type) {
            case hasp_gpio_type_t::LED_R... hasp_gpio_type_t::LED_W:
                LOG_VERBOSE(TAG_GPIO, F(D_BULLET D_GPIO_PIN " %d => %d"), gpioConfig[i].pin, gpioConfig[i].val);
                break;
        }
    }

    // TODO: Update objects when the Mood Color Pin is in a group
}

bool gpioIsSystemPin(uint8_t gpio)
{
    if((gpio >= NUM_DIGITAL_PINS) // invalid pins

// Use individual checks instead of switch statement, as some case labels could be duplicated
#ifdef TOUCH_CS
       || (gpio == TOUCH_CS)
#endif
#ifdef TFT_MOSI
       || (gpio == TFT_MOSI)
#endif
#ifdef TFT_MISO
       || (gpio == TFT_MISO)
#endif
#ifdef TFT_SCLK
       || (gpio == TFT_SCLK)
#endif
#ifdef TFT_CS
       || (gpio == TFT_CS)
#endif
#ifdef TFT_DC
       || (gpio == TFT_DC)
#endif
#ifdef TFT_BL
       || (gpio == TFT_BL)
#endif
#ifdef TFT_RST
       || (gpio == TFT_RST)
#endif
#ifdef TFT_WR
       || (gpio == TFT_WR)
#endif
#ifdef TFT_RD
       || (gpio == TFT_RD)
#endif
#ifdef TFT_D0
       || (gpio == TFT_D0)
#endif
#ifdef TFT_D1
       || (gpio == TFT_D1)
#endif
#ifdef TFT_D2
       || (gpio == TFT_D2)
#endif
#ifdef TFT_D3
       || (gpio == TFT_D3)
#endif
#ifdef TFT_D4
       || (gpio == TFT_D4)
#endif
#ifdef TFT_D5
       || (gpio == TFT_D5)
#endif
#ifdef TFT_D6
       || (gpio == TFT_D6)
#endif
#ifdef TFT_D7
       || (gpio == TFT_D7)
#endif
#ifdef TFT_D8
       || (gpio == TFT_D8)
#endif
#ifdef TFT_D9
       || (gpio == TFT_D9)
#endif
#ifdef TFT_D10
       || (gpio == TFT_D10)
#endif
#ifdef TFT_D11
       || (gpio == TFT_D11)
#endif
#ifdef TFT_D12
       || (gpio == TFT_D12)
#endif
#ifdef TFT_D13
       || (gpio == TFT_D13)
#endif
#ifdef TFT_D14
       || (gpio == TFT_D14)
#endif
#ifdef TFT_D15
       || (gpio == TFT_D15)
#endif
    ) {
        return true;
    } // if tft_espi pins

    // To-do:
    // Backlight GPIO
    // Network GPIOs
    // Serial GPIOs
    // Tasmota Client GPIOs

#ifdef ARDUINO_ARCH_ESP32
    if((gpio >= 6) && (gpio <= 11)) return true;  // integrated SPI flash
    if((gpio == 37) || (gpio == 38)) return true; // unavailable
    if(psramFound()) {
        if((gpio == 16) || (gpio == 17)) return true; // PSRAM
    }
#endif

#ifdef ARDUINO_ARCH_ESP8266
    if((gpio >= 6) && (gpio <= 11)) return true; // integrated SPI flash
#ifndef TFT_SPI_OVERLAP
    if((gpio >= 12) && (gpio <= 14)) return true; // HSPI
#endif
#endif

    return false;
}

bool gpioInUse(uint8_t pin)
{
    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        if((gpioConfig[i].pin == pin) && gpioConfigInUse(i)) {
            return true; // pin matches and is in use
        }
    }

    return false;
}

bool gpioSavePinConfig(uint8_t config_num, uint8_t pin, uint8_t type, uint8_t group, uint8_t pinfunc, bool inverted)
{
    // TODO: Input validation

    // ESP8266: Only Pullups except on gpio16

    // ESP32: Pullup or Pulldown except on 34-39

    if(config_num < HASP_NUM_GPIO_CONFIG && !gpioIsSystemPin(pin)) {
        gpioConfig[config_num].pin           = pin;
        gpioConfig[config_num].type          = type;
        gpioConfig[config_num].group         = group;
        gpioConfig[config_num].gpio_function = pinfunc;
        gpioConfig[config_num].inverted      = inverted;
        LOG_TRACE(TAG_GPIO, F("Saving Pin config #%d pin %d - type %d - group %d - func %d"), config_num, pin, type,
                  group, pinfunc);
        return true;
    }

    return false;
}

bool gpioConfigInUse(uint8_t num)
{
    if(num >= HASP_NUM_GPIO_CONFIG) return false;
    return gpioConfig[num].type != hasp_gpio_type_t::FREE;
}

int8_t gpioGetFreeConfigId()
{
    uint8_t id = 0;
    while(id < HASP_NUM_GPIO_CONFIG) {
        if(!gpioConfigInUse(id)) return id;
        id++;
    }
    return -1;
}

hasp_gpio_config_t gpioGetPinConfig(uint8_t num)
{
    return gpioConfig[num];
}

void gpio_discovery(JsonObject& input, JsonArray& relay, JsonArray& light, JsonArray& dimmer)
{
    char description[20];

    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        switch(gpioConfig[i].type) {
            case hasp_gpio_type_t::LIGHT_RELAY:
                light.add(gpioConfig[i].pin);
                break;

            case hasp_gpio_type_t::POWER_RELAY:
                relay.add(gpioConfig[i].pin);
                break;

            case hasp_gpio_type_t::DAC:
            case hasp_gpio_type_t::LED: // Don't include the moodlight
            case hasp_gpio_type_t::SERIAL_DIMMER:
            case hasp_gpio_type_t::SERIAL_DIMMER_AU:
            case hasp_gpio_type_t::SERIAL_DIMMER_EU:
                dimmer.add(gpioConfig[i].pin);
                break;

            case SWITCH:
                strcpy_P(description, PSTR("none"));
                break;
            case BATTERY:
                strcpy_P(description, PSTR("battery"));
                break;
            case BATTERY_CHARGING:
                strcpy_P(description, PSTR("battery_charging"));
                break;
            case COLD:
                strcpy_P(description, PSTR("cold"));
                break;
            case CONNECTIVITY:
                strcpy_P(description, PSTR("connectivity"));
                break;
            case DOOR:
                strcpy_P(description, PSTR("door"));
                break;
            case GARAGE_DOOR:
                strcpy_P(description, PSTR("garage_door"));
                break;
            case GAS:
                strcpy_P(description, PSTR("gas"));
                break;
            case HEAT:
                strcpy_P(description, PSTR("heat"));
                break;
            case LIGHT:
                strcpy_P(description, PSTR("light"));
                break;
            case LOCK:
                strcpy_P(description, PSTR("lock"));
                break;
            case MOISTURE:
                strcpy_P(description, PSTR("moisture"));
                break;
            case MOTION:
                strcpy_P(description, PSTR("motion"));
                break;
            case MOVING:
                strcpy_P(description, PSTR("moving"));
                break;
            case OCCUPANCY:
                strcpy_P(description, PSTR("occupancy"));
                break;
            case OPENING:
                strcpy_P(description, PSTR("opening"));
                break;
            case PLUG:
                strcpy_P(description, PSTR("plug"));
                break;
            case POWER:
                strcpy_P(description, PSTR("power"));
                break;
            case PRESENCE:
                strcpy_P(description, PSTR("presence"));
                break;
            case PROBLEM:
                strcpy_P(description, PSTR("problem"));
                break;
            case SAFETY:
                strcpy_P(description, PSTR("safety"));
                break;
            case SMOKE:
                strcpy_P(description, PSTR("smoke"));
                break;
            case SOUND:
                strcpy_P(description, PSTR("sound"));
                break;
            case VIBRATION:
                strcpy_P(description, PSTR("vibration"));
                break;
            case WINDOW:
                strcpy_P(description, PSTR("window"));
                break;
            case hasp_gpio_type_t::FREE:
            default:
                break;
        }

        if(gpioConfig[i].type >= hasp_gpio_type_t::SWITCH && gpioConfig[i].type <= hasp_gpio_type_t::WINDOW) {
            JsonArray arr = input[description];
            if(arr.isNull()) arr = input.createNestedArray(description);
            arr.add(gpioConfig[i].pin);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
#if HASP_USE_CONFIG > 0
bool gpioGetConfig(const JsonObject& settings)
{
    bool changed = false;

    /* Check Gpio array has changed */
    JsonArray array = settings[FPSTR(FP_GPIO_CONFIG)].as<JsonArray>();
    uint8_t i       = 0;
    for(JsonVariant v : array) {
        if(i < HASP_NUM_GPIO_CONFIG) {
            uint32_t cur_val = gpioConfig[i].pin | (gpioConfig[i].group << 8) | (gpioConfig[i].type << 16) |
                               (gpioConfig[i].gpio_function << 24) | (gpioConfig[i].inverted << 31);
            LOG_INFO(TAG_GPIO, F("GPIO CONF: %d: %d <=> %d"), i, cur_val, v.as<uint32_t>());

            if(cur_val != v.as<uint32_t>()) changed = true;
            v.set(cur_val);
        } else {
            changed = true;
        }
        i++;
    }

    /* Build new Gpio array if the count is not correct */
    if(i != HASP_NUM_GPIO_CONFIG) {
        array = settings[FPSTR(FP_GPIO_CONFIG)].to<JsonArray>(); // Clear JsonArray
        for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
            uint32_t cur_val = gpioConfig[i].pin | (gpioConfig[i].group << 8) | (gpioConfig[i].type << 16) |
                               (gpioConfig[i].gpio_function << 24) | (gpioConfig[i].inverted << 31);
            array.add(cur_val);
        }
        changed = true;
    }

    if(changed) configOutput(settings);
    return changed;
}

/** Set GPIO Configuration.
 *
 * Read the settings from json and sets the application variables.
 *
 * @note: data pixel should be formated to uint32_t RGBA. Imagemagick requirements.
 *
 * @param[in] settings    JsonObject with the config settings.
 **/
bool gpioSetConfig(const JsonObject& settings)
{
    configOutput(settings);
    bool changed = false;

    if(!settings[FPSTR(FP_GPIO_CONFIG)].isNull()) {
        bool status = false;
        int i       = 0;

        JsonArray array = settings[FPSTR(FP_GPIO_CONFIG)].as<JsonArray>();
        for(JsonVariant v : array) {
            uint32_t new_val = v.as<uint32_t>();

            if(i < HASP_NUM_GPIO_CONFIG) {
                uint32_t cur_val = gpioConfig[i].pin | (gpioConfig[i].group << 8) | (gpioConfig[i].type << 16) |
                                   (gpioConfig[i].gpio_function << 24) | (gpioConfig[i].inverted << 31);
                if(cur_val != new_val) status = true;

                gpioConfig[i].pin           = new_val & 0xFF;
                gpioConfig[i].group         = new_val >> 8 & 0xFF;
                gpioConfig[i].type          = new_val >> 16 & 0xFF;
                gpioConfig[i].gpio_function = new_val >> 24 & 0x7F;
                gpioConfig[i].inverted      = new_val >> 31 & 0x1;
            }
            i++;
        }
        changed |= status;
    }

    return changed;
}
#endif // HASP_USE_CONFIG