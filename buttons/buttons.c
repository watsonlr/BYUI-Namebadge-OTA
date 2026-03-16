#include "buttons.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

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
    /* gpio_reset_pin: revokes any peripheral claim on the pad, sets
     * MCU_SEL = GPIO function, enables pull-up, disables input buffer.
     * This is required to recover pins that the PSRAM/MSPI init left
     * driven or in a non-GPIO IO_MUX function before app_main. */
    for (int i = 0; i < (int)BTN_COUNT; i++) {
        gpio_reset_pin(BTN_MAP[i].pin);
    }

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

    /* Prevent the sleep_gpio auto-switch (enabled at boot for all pins) from
     * clobbering our active pull-up config when the CPU idles.  Set the sleep
     * shadow registers to the same INPUT + PULLUP mode so the active state is
     * preserved regardless of CPU idle depth. */
    for (int i = 0; i < (int)BTN_COUNT; i++) {
        gpio_sleep_set_direction(BTN_MAP[i].pin, GPIO_MODE_INPUT);
        gpio_sleep_set_pull_mode(BTN_MAP[i].pin, GPIO_PULLUP_ONLY);
    }

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
