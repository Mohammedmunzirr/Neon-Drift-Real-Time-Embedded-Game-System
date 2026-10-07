/*

 * graphics.c - MUNZIR'S FILE
 * Responsibilities:
 *   1. Initialize all output devices:
 *      OLED, NeoPixel ring, RGB LED, and buzzer
 *   2. task_oled_render: renders game state on OLED + drives
 *      RGB LED and buzzer every 50ms on Core 1
 *   3. task_neopixel: drives NeoPixel ring every 80ms on Core 1
 *      (separate task — different bus, lower urgency)

 */

#include "game.h"

static const char* TAG = "GRAPHICS";

/* Extra RGB LED pins from diagram.json. LED_PIN is the red pin. */
#define RGB_RED_PIN     LED_PIN        // GPIO25
#define RGB_GREEN_PIN   GPIO_NUM_26
#define RGB_BLUE_PIN    GPIO_NUM_27

/*
 * RMT timing for WS2812B.
 * clk_div = 8 → 80MHz / 8 = 10MHz → 1 tick = 100ns
 */
#define RMT_TX_CHANNEL  RMT_CHANNEL_0
#define RMT_CLK_DIV     8
#define WS_T0H          4      // 400ns
#define WS_T0L          9      // 900ns
#define WS_T1H          8      // 800ns
#define WS_T1L          5      // 500ns

/* LEDC channels used for RGB LED and buzzer */
#define LEDC_RGB_TIMER      LEDC_TIMER_0
#define LEDC_BUZZER_TIMER   LEDC_TIMER_1
#define LEDC_RED_CHANNEL    LEDC_CHANNEL_0
#define LEDC_GREEN_CHANNEL  LEDC_CHANNEL_1
#define LEDC_BLUE_CHANNEL   LEDC_CHANNEL_2
#define LEDC_BUZZER_CHANNEL LEDC_CHANNEL_3

/* Internal pixel buffer: [pixel][G, R, B] — WS2812B is GRB order */
static uint8_t ringPixels[NUM_PIXELS][3];

//HELPERS
static int clamp_int(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/* Scale 0-255 to 0-1023 for 10-bit LEDC duty cycle */
static uint32_t scale_duty(uint8_t value) {
    return (uint32_t)((value * 1023) / 255);
}

//RGB LED
static void rgb_set(uint8_t red, uint8_t green, uint8_t blue) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_RED_CHANNEL,   scale_duty(red));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_RED_CHANNEL);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_GREEN_CHANNEL, scale_duty(green));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_GREEN_CHANNEL);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_BLUE_CHANNEL,  scale_duty(blue));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_BLUE_CHANNEL);
}

//BUZZER
static void buzzer_set(int frequency, bool on) {
    if (!on) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_BUZZER_CHANNEL, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_BUZZER_CHANNEL);
        return;
    }
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_BUZZER_TIMER, frequency);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_BUZZER_CHANNEL, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_BUZZER_CHANNEL);
}

//NEOPIXEL RING — buffer helpers
static void ring_clear(void) {
    memset(ringPixels, 0, sizeof(ringPixels));
}

/* Store in GRB order as required by WS2812B */
static void ring_set_pixel(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index < 0 || index >= NUM_PIXELS) return;
    ringPixels[index][0] = green;
    ringPixels[index][1] = red;
    ringPixels[index][2] = blue;
}

/* Convert pixel buffer to RMT pulse items and transmit */
static void ring_show(void) {
    rmt_item32_t items[NUM_PIXELS * 24];
    int itemIndex = 0;

    for (int pixel = 0; pixel < NUM_PIXELS; pixel++) {
        for (int color = 0; color < 3; color++) {
            uint8_t value = ringPixels[pixel][color];
            for (int bit = 7; bit >= 0; bit--) {
                bool one = (value >> bit) & 0x01;
                items[itemIndex].level0    = 1;
                items[itemIndex].duration0 = one ? WS_T1H : WS_T0H;
                items[itemIndex].level1    = 0;
                items[itemIndex].duration1 = one ? WS_T1L : WS_T0L;
                itemIndex++;
            }
        }
    }

    rmt_write_items(RMT_TX_CHANNEL, items, itemIndex, true);
    rmt_wait_tx_done(RMT_TX_CHANNEL, pdMS_TO_TICKS(20));
}

/*
 * ring_render: lights LEDs proportional to boostEnergy.
 * Color depends on current game mode flags.
 * Called only from task_neopixel.
 */
static void ring_render(int boostEnergy,
                        bool isGameOver,
                        bool isNightmare,
                        bool isBoosting) {
    int litPixels = (boostEnergy * NUM_PIXELS) / 100;
    litPixels = clamp_int(litPixels, 0, NUM_PIXELS);

    ring_clear();
    for (int i = 0; i < litPixels; i++) {
        if (isGameOver) {
            ring_set_pixel(i, 70, 0, 0);       /* Dark red — crashed */
        } else if (isNightmare) {
            ring_set_pixel(i, 40, 0, 55);      /* Purple — nightmare mode */
        } else if (isBoosting) {
            ring_set_pixel(i, 0, 70, 90);      /* Teal — boosting */
        } else {
            ring_set_pixel(i, 0, 80, 25);      /* Green — normal */
        }
    }

    ring_show();
}

// OLED DRAW FUNCTIONS
static void draw_hud(const GameState* state) {
    char text[24];

    snprintf(text, sizeof(text), "SCORE:%d", state->score);
    fb_string(0, 0, text);

    snprintf(text, sizeof(text), "B :%d", state->boostEnergy);
    fb_string(82, 0, text);

    /* Shield phase bar (bottom left) */
    int phaseWidth = (state->shieldPhase * 30) / 255;
    fb_frame(0, 56, 32, 7);
    fb_rect(1, 57, clamp_int(phaseWidth, 0, 30), 5);

    /* LDR bar (bottom right) — maps 0-4095 to 0-30 pixels */
    int ldrWidth = (state->ldrValue * 30) / 4095;
    fb_frame(95, 56, 32, 7);
    fb_rect(96, 57, clamp_int(ldrWidth, 0, 30), 5);
}

static void draw_obstacles(const GameState* state) {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!state->obstacles[i].active) continue;
        const Obstacle* obstacle = &state->obstacles[i];
        fb_rect(obstacle->x, obstacle->y, obstacle->w, obstacle->h);
    }
}

static void draw_nightmare_overlay(void) {
    fb_frame(0, 8, SCREEN_W, SCREEN_H - 16);
    for (int x = 0; x < SCREEN_W; x += 8) {
        fb_pixel(x, 12, true);
        fb_pixel(SCREEN_W - 1 - x, 51, true);
    }
}

static void draw_game_over(const GameState* state) {
    char text[24];
    fb_frame(22, 18, 84, 30);
    fb_string(37, 25, "GAME OVER");
    snprintf(text, sizeof(text), "SCORE:%d", state->score);
    fb_string(34, 37, text);
}

static void oled_render(const GameState* state,
                        bool isGameOver,
                        bool isNightmare,
                        bool isBoosting,
                        bool isPaused) {
    fb_clear();

    draw_hud(state);
    draw_obstacles(state);
    fb_ship(state->shipX, state->shipY, state->shipW);

    if (isNightmare) {
        draw_nightmare_overlay();
    }

    if (isBoosting) {
        fb_string(45, 56, "BST");
    }

    if (isPaused) {
        fb_frame(22, 24, 84, 18);
        fb_string(40, 29, "PAUSED");
    }

    if (isGameOver) {
        draw_game_over(state);
    }

    oled_flush();
}

/* 
 * OUTPUTS RENDER — RGB LED + buzzer only.
 * NeoPixel ring is handled separately by task_neopixel.
 */
static void outputs_render(const GameState* state,
                           bool isGameOver,
                           bool isNightmare,
                           bool isBoosting,
                           bool isPaused) {
    uint8_t phase = (uint8_t)clamp_int(state->shieldPhase, 0, 255);

    // State variables to track time for our sound effects
    static bool prevGameOver = false;
    static int  t = 0; 

    if (isGameOver) {
        rgb_set(255, 0, 0);

        if (!prevGameOver) { 
            t = 0; 
            prevGameOver = true;
        }

        t++;
        // Non-musical industrial "System Fault" alarm (Harsh 150Hz buzz)
        if      (t < 3)   buzzer_set(150, true);   // Error buzz
        else if (t < 5)   buzzer_set(0,   false);  // Silence gap
        else if (t < 8)   buzzer_set(150, true);   // Error buzz
        else if (t < 10)  buzzer_set(0,   false);  // Silence gap
        else if (t < 15)  buzzer_set(150, true);   // Long error buzz
        else              buzzer_set(0,   false);  // done: duty=0, silence

        return;
    }

    if (isPaused) {
        rgb_set(0, 0, 255);       // Set RGB LED to Solid Blue
        buzzer_set(0, false);     // Silence the buzzer
        return;                   // Skip the rest of the checks
    }

    // Not in game-over: reset the tone so the next death plays it
    prevGameOver = false;
    t = 0;

    if (isNightmare) {
        rgb_set(phase / 2, 0, phase);
        // Low mechanical engine rumble (80Hz)
        buzzer_set(80, isBoosting);
        
    } else if (isBoosting) {
        rgb_set(0, phase, 255);
        
        // Continuous, flat mid-range "Beeeeeeeeep"
        // 500Hz is perfectly in the middle: not shrill, not dull. 
        buzzer_set(500, true);
        
    } else {
        rgb_set(0, phase, 255 - phase);
        buzzer_set(0, false);
    }
    /* NeoPixel ring is NOT updated here — task_neopixel owns it */
}

//HARDWARE INIT FUNCTIONS
static void init_rgb_led(void) {
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_RGB_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t red = {
        .gpio_num   = RGB_RED_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_RED_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_RGB_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&red);

    ledc_channel_config_t green = red;
    green.gpio_num = RGB_GREEN_PIN;
    green.channel  = LEDC_GREEN_CHANNEL;
    ledc_channel_config(&green);

    ledc_channel_config_t blue = red;
    blue.gpio_num = RGB_BLUE_PIN;
    blue.channel  = LEDC_BLUE_CHANNEL;
    ledc_channel_config(&blue);
}

static void init_buzzer(void) {
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_BUZZER_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num   = BUZZER_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_BUZZER_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_BUZZER_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&channel);
}

static void init_neopixel(void) {
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(NEOPIXEL_PIN, RMT_TX_CHANNEL);
    config.clk_div = RMT_CLK_DIV;
    rmt_config(&config);
    rmt_driver_install(RMT_TX_CHANNEL, 0, 0);

    ring_clear();
    ring_show();
}

static void init_outputs(void) {
    oled_init();        /* I2C + SSD1306 init */
    init_rgb_led();     /* LEDC PWM for R/G/B channels */
    init_buzzer();      /* LEDC PWM for buzzer */
    init_neopixel();    /* RMT for WS2812B ring */

    rgb_set(0, 0, 0);
    buzzer_set(0, false);
    ESP_LOGI(TAG, "Output devices initialized");
}

/* ============================================================
 * task_oled_render — OLED display + RGB LED + Buzzer
 *
 * Runs on Core 1, priority PRIO_OLED_RENDER, every 50ms.
 * Takes a snapshot of GameState under mutex then releases
 * immediately before doing slow I2C work with local copy.
 * Reads gameEventGroup for mode flags.
 * ============================================================ */
void task_oled_render(void* arg) {
    (void)arg;

    init_outputs();     /* Initialize all hardware once at startup */

    while (true) {
        /* Read event flags without holding the mutex */
        EventBits_t bits = xEventGroupGetBits(gameEventGroup);
        bool isGameOver  = (bits & EVT_GAME_OVER)      != 0;
        bool isNightmare = (bits & EVT_NIGHTMARE_MODE)  != 0;
        bool isBoosting  = (bits & EVT_BOOST_ACTIVE)    != 0;
        bool isPaused    = (bits & EVT_PAUSED)          != 0;

        /* Snapshot GameState — hold mutex only for the memcpy */
        GameState snapshot;
        if (gameMutex != NULL &&
            xSemaphoreTake(gameMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            memcpy(&snapshot, (const void*)&game, sizeof(GameState));
            xSemaphoreGive(gameMutex);

            /* Render with local copies — mutex is already released */
            oled_render(&snapshot, isGameOver, isNightmare, isBoosting, isPaused);
            outputs_render(&snapshot, isGameOver, isNightmare, isBoosting, isPaused);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* task_neopixel — NeoPixel ring only
 *
 * Runs on Core 1, priority PRIO_NEOPIXEL, every 80ms.
 * Separated from task_oled_render because:
 *   - RMT and I2C are independent hardware buses — no need to
 *     block one while the other is transmitting
 *   - NeoPixel is cosmetic, lower urgency than the display
 *   - Each bus runs at its own natural rate (50ms vs 80ms)
 *
 */
void task_neopixel(void* arg) {
    (void)arg;

    while (true) {
        /* Read event flags — no mutex needed */
        EventBits_t bits = xEventGroupGetBits(gameEventGroup);
        bool isGameOver  = (bits & EVT_GAME_OVER)      != 0;
        bool isNightmare = (bits & EVT_NIGHTMARE_MODE)  != 0;
        bool isBoosting  = (bits & EVT_BOOST_ACTIVE)    != 0;

        /* Only need boostEnergy from GameState */
        int energy = 100;
        if (gameMutex != NULL &&
            xSemaphoreTake(gameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            energy = game.boostEnergy;
            xSemaphoreGive(gameMutex);
        }

        ring_render(energy, isGameOver, isNightmare, isBoosting);

        vTaskDelay(pdMS_TO_TICKS(80));
    }
}
