#include "buttons.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "soc/gpio_periph.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"

#define TAG "buttons"

/* ── GPIO assignments ──────────────────────────────────────────────── */
#define GPIO_UP     17
#define GPIO_DOWN   16
#define GPIO_LEFT   14
#define GPIO_RIGHT  15
#define GPIO_A      38
#define GPIO_B      18

#define POLL_MS     20
#define DEBOUNCE_MS 30

static const struct {
    gpio_num_t pin;
    button_t   bit;
    const char *name;
} BTN_MAP[] = {
    { GPIO_UP,    BTN_UP,    "UP"    },
    { GPIO_DOWN,  BTN_DOWN,  "DOWN"  },
    { GPIO_LEFT,  BTN_LEFT,  "LEFT"  },
    { GPIO_RIGHT, BTN_RIGHT, "RIGHT" },
    { GPIO_A,     BTN_A,     "A"     },
    { GPIO_B,     BTN_B,     "B"     },
};

#define BTN_COUNT  (sizeof(BTN_MAP) / sizeof(BTN_MAP[0]))

/* ── Public API ────────────────────────────────────────────────────── */

void buttons_init(void)
{
    /* Clear any GPIO pad hold that sleep_gpio may have set before app_main.
     * This releases both RTC_CNTL_PAD_HOLD_REG and DIG_PAD_HOLD for all pads,
     * allowing subsequent gpio_config writes to take effect. */
    gpio_force_unhold_all();

    /* gpio_reset_pin: revokes any peripheral claim on the pad, sets
     * MCU_SEL = GPIO function, enables pull-up, disables input buffer.
     * This is required to recover pins that the PSRAM/MSPI init left
     * driven or in a non-GPIO IO_MUX function before app_main. */
    for (int i = 0; i < (int)BTN_COUNT; i++) {
        gpio_reset_pin(BTN_MAP[i].pin);
    }

    /* Diagnostic: state immediately after gpio_reset_pin (before gpio_config) */
    ESP_LOGW(TAG, "PRE-CFG ENABLE=0x%08x ENABLE1=0x%08x",
             (unsigned)REG_READ(GPIO_ENABLE_REG),
             (unsigned)REG_READ(GPIO_ENABLE1_REG));
    ESP_LOGW(TAG, "PRE-CFG IN=0x%08x IN1=0x%08x",
             (unsigned)REG_READ(GPIO_IN_REG),
             (unsigned)REG_READ(GPIO_IN1_REG));

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_UP)    | (1ULL << GPIO_DOWN)  |
                        (1ULL << GPIO_LEFT)   | (1ULL << GPIO_RIGHT) |
                        (1ULL << GPIO_A)      | (1ULL << GPIO_B),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    /* sleep_gpio (runs at ~434 ms, before app_main) writes SLP_SEL=1 into every
     * IO_MUX register and sets the sleep-shadow to DISABLED+FLOATING.
     * With SLP_SEL=1 the IO_MUX routes the pad signal to the LP subsystem
     * instead of the GPIO matrix, so gpio_get_level() always returns 0.
     *
     * Fix:
     *  1. Set sleep shadow (MCU_xxx) to INPUT+PULLUP so the pin behaves
     *     correctly if the chip ever enters light sleep.
     *  2. Clear SLP_SEL → restores active (FUN_xxx) path through the GPIO
     *     matrix, making gpio_get_level() work again. */
    for (int i = 0; i < (int)BTN_COUNT; i++) {
        gpio_sleep_set_direction(BTN_MAP[i].pin, GPIO_MODE_INPUT);
        gpio_sleep_set_pull_mode(BTN_MAP[i].pin, GPIO_PULLUP_ONLY);
        PIN_SLP_SEL_DISABLE(GPIO_PIN_MUX_REG[BTN_MAP[i].pin]);
    }

    /* Diagnostic: briefly drive each pin HIGH (output), read back, restore input.
     * If GPIO_IN shows 1 when driven HIGH → GPIO matrix path works, something
     * external is holding the pin LOW in input mode.
     * If GPIO_IN stays 0 even when driven HIGH → GPIO matrix path is broken. */
    for (int i = 0; i < (int)BTN_COUNT; i++) {
        gpio_num_t pin = BTN_MAP[i].pin;
        /* GPIO_MODE_INPUT_OUTPUT keeps FUN_IE=1 so gpio_get_level reads pad */
        gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
        PIN_SLP_SEL_DISABLE(GPIO_PIN_MUX_REG[pin]);
        /* Read ENABLE_REG right now — if the bit is 0 despite OUTPUT mode,
         * digital pad hold is blocking the write. */
        uint32_t en_val = (pin < 32) ? REG_READ(GPIO_ENABLE_REG)
                                     : REG_READ(GPIO_ENABLE1_REG);
        int en_bit = (en_val >> (pin < 32 ? pin : (pin - 32))) & 1;
        gpio_set_level(pin, 1);
        int driven_hi = gpio_get_level(pin);
        gpio_set_level(pin, 0);
        int driven_lo = gpio_get_level(pin);
        /* Restore to input+pullup */
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_pullup_en(pin);
        PIN_SLP_SEL_DISABLE(GPIO_PIN_MUX_REG[pin]);
        int as_input = gpio_get_level(pin);
        ESP_LOGW(TAG, "pin%d: en=%d drv_hi=%d drv_lo=%d as_input=%d",
                 pin, en_bit, driven_hi, driven_lo, as_input);
    }

    /* Diagnostic: dump GPIO output enable and func_out_sel_cfg for each button pin */
    ESP_LOGW(TAG, "ENABLE=0x%08x ENABLE1=0x%08x OUT=0x%08x OUT1=0x%08x",
             (unsigned)REG_READ(GPIO_ENABLE_REG),
             (unsigned)REG_READ(GPIO_ENABLE1_REG),
             (unsigned)REG_READ(GPIO_OUT_REG),
             (unsigned)REG_READ(GPIO_OUT1_REG));
    for (int i = 0; i < (int)BTN_COUNT; i++) {
        gpio_num_t pin = BTN_MAP[i].pin;
        uint32_t mux = REG_READ(GPIO_PIN_MUX_REG[pin]);
        uint32_t sel = REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + pin * 4);
        ESP_LOGW(TAG, "pin%d mux=0x%04x(IE=%d PU=%d SLP=%d) sel=0x%04x(sig=%d oen_sel=%d) level=%d",
                 pin, (unsigned)mux,
                 (int)((mux >> 9) & 1), (int)((mux >> 8) & 1), (int)((mux >> 1) & 1),
                 (unsigned)sel, (int)(sel & 0x1ff), (int)((sel >> 10) & 1),
                 gpio_get_level(pin));
    }
    ESP_LOGW(TAG, "GPIO_IN=0x%08x IN1=0x%08x",
             (unsigned)REG_READ(GPIO_IN_REG),
             (unsigned)REG_READ(GPIO_IN1_REG));

    /* Allow pull-ups to charge the lines before the first read. */
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGW(TAG, "buttons raw after init: 0x%02x", (unsigned)buttons_read());
}

button_t buttons_read(void)
{
    button_t state = BTN_NONE;
    for (int i = 0; i < (int)BTN_COUNT; i++) {
        if (gpio_get_level(BTN_MAP[i].pin) == 0) {
            state |= BTN_MAP[i].bit;
        }
    }
    return state;
}

bool buttons_held(button_t mask, uint32_t duration_ms)
{
    uint32_t held_ms = 0;

    while (held_ms < duration_ms) {
        if ((buttons_read() & mask) != mask) {
            return false;   /* any button in the mask released → fail */
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        held_ms += POLL_MS;
    }
    return true;
}

button_t buttons_wait_press(uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    button_t   last  = buttons_read();  /* snapshot — stuck-LOW pins won't fire */

    for (;;) {
        /* Timeout check */
        if (timeout_ms > 0) {
            uint32_t elapsed = (uint32_t)((xTaskGetTickCount() - start)
                                          * portTICK_PERIOD_MS);
            if (elapsed >= timeout_ms) {
                return BTN_NONE;
            }
        }

        button_t cur     = buttons_read();
        button_t pressed = cur & ~last;   /* newly pressed buttons this poll */
        last = cur;

        if (pressed) {
            /* Debounce: re-read after DEBOUNCE_MS */
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            button_t confirmed = buttons_read() & pressed;
            if (confirmed) {
                /* Wait for full release so caller doesn't re-trigger */
                while (buttons_read() & confirmed) {
                    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
                }
                return confirmed;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
