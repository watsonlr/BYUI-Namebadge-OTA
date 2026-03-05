/**
 * @file splash_screen.h
 * @brief Scroll-up splash screen for the BYUI eBadge ILI9341 display.
 *
 * Call splash_screen_run() once near the start of app_main().
 * It initialises SPI2 + the ILI9341, plays the scroll-up animation,
 * holds the image briefly, then returns.
 *
 * Before building, run png_to_rgb565.py to generate image_rgb565.h
 * and place that file in this component directory.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the splash-screen animation to completion.
 *
 * Initialises SPI2 and the ILI9341 display, then reveals the image
 * row-by-row from the bottom of the screen upward (scroll-up effect).
 * After the full image is visible it holds for SPLASH_HOLD_MS then
 * returns.
 *
 * The SPI bus (SPI2_HOST) is left initialised so the caller can add
 * the SD-card device to the same bus afterwards if needed.
 */
void splash_screen_run(void);

#ifdef __cplusplus
}
#endif
