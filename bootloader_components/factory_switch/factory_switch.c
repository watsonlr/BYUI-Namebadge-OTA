/**
 * @file factory_switch.c
 * @brief Bootloader hook: hold A+B at power-on to force factory boot.
 *
 * Runs inside the second-stage bootloader via bootloader_after_init().
 * If both A (GPIO 38) and B (GPIO 18) are held LOW for HOLD_MS, the
 * OTA data partition is erased so the ESP-IDF bootloader falls back to
 * the factory app on this and every subsequent boot — until a new OTA
 * download re-writes the OTA data.
 */

#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "esp_rom_spiflash.h"
#include "soc/gpio_reg.h"

#define BTN_A_GPIO   38
#define BTN_B_GPIO   18
#define HOLD_MS      200

/* OTA data partition: two 4 KB sectors at 0xF000 (matches partitions.csv). */
#define OTADATA_SECTOR_0  (0x0F000u / 4096u)   /* sector 15 */
#define OTADATA_SECTOR_1  (0x10000u / 4096u)   /* sector 16 */

static inline int read_gpio(int gpio)
{
    if (gpio < 32)
        return (int)((REG_READ(GPIO_IN_REG)  >> gpio)        & 1u);
    return     (int)((REG_READ(GPIO_IN1_REG) >> (gpio - 32)) & 1u);
}

/* Defined as weak in the bootloader — our strong definition overrides it. */
void bootloader_after_init(void)
{
    esp_rom_gpio_pad_select_gpio(BTN_A_GPIO);
    esp_rom_gpio_pad_pullup_only(BTN_A_GPIO);
    esp_rom_gpio_pad_select_gpio(BTN_B_GPIO);
    esp_rom_gpio_pad_pullup_only(BTN_B_GPIO);

    for (int ms = 0; ms < HOLD_MS; ms++) {
        if (read_gpio(BTN_A_GPIO) != 0 || read_gpio(BTN_B_GPIO) != 0)
            return;   /* a button released — boot normally */
        esp_rom_delay_us(1000);
    }

    /* Both held the full window — erase OTA data → factory boot. */
    esp_rom_spiflash_erase_sector(OTADATA_SECTOR_0);
    esp_rom_spiflash_erase_sector(OTADATA_SECTOR_1);
}
