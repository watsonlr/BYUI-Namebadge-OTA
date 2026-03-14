#include "buttons.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
} BTN_MAP[] = {
    { GPIO_UP,    BTN_UP    },
    { GPIO_DOWN,  BTN_DOWN  },
    { GPIO_LEFT,  BTN_LEFT  },
    { GPIO_RIGHT, BTN_RIGHT },
    { GPIO_A,     BTN_A     },
    { GPIO_B,     BTN_B     },
};

#define BTN_COUNT  (sizeof(BTN_MAP) / sizeof(BTN_MAP[0]))

/* ── Public API ────────────────────────────────────────────────────── */

void buttons_init(void)
{
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
    button_t   last  = BTN_NONE;

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
