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
    /* Reset each pin first — disconnects IO_MUX peripheral assignments. */
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

    /* Per-pin settle monitoring: log when each pin goes HIGH and when it
     * gets stuck.  Runs up to 3000 ms so slow or stuck pins are visible. */
    bool settled[BTN_COUNT] = {0};
    int  settled_at[BTN_COUNT] = {0};
    int  all_settled_ms = -1;

    for (int t = 0; t <= 3000; t += POLL_MS) {
        bool all = true;
        for (int i = 0; i < (int)BTN_COUNT; i++) {
            int lv = gpio_get_level(BTN_MAP[i].pin);
            if (lv == 1 && !settled[i]) {
                settled[i] = true;
                settled_at[i] = t;
                ESP_LOGW(TAG, "  GPIO%d (%s) went HIGH at t=%d ms",
                         BTN_MAP[i].pin, BTN_MAP[i].name, t);
            }
            if (!settled[i]) all = false;
        }
        if (all) {
            all_settled_ms = t;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }

    if (all_settled_ms >= 0) {
        ESP_LOGW(TAG, "All buttons settled at %d ms", all_settled_ms);
    } else {
        ESP_LOGE(TAG, "TIMEOUT: buttons still LOW after 3000 ms:");
        for (int i = 0; i < (int)BTN_COUNT; i++) {
            if (!settled[i]) {
                ESP_LOGE(TAG, "  GPIO%d (%s) STUCK LOW",
                         BTN_MAP[i].pin, BTN_MAP[i].name);
            }
        }
    }
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
    button_t   last  = buttons_read();  /* snapshot so always-LOW pins don't fire */

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
