// Shared Header File (game.h)
// Contains all pin assignments, came constants, gamestate layout, function prototypes

#ifndef GAME_H
#define GAME_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/rmt.h"
#include "esp_log.h"
#include "esp_random.h"

// Pin Assignments

/* Sensor pins (Ahmad's) */
#define JOY_X_PIN     ADC1_CHANNEL_6   // GPIO34
#define JOY_Y_PIN     ADC1_CHANNEL_7   // GPIO35
#define JOY_SEL_PIN   GPIO_NUM_15 // Boost Button
#define POT_PIN       ADC1_CHANNEL_4   // GPIO32
#define LDR_PIN       ADC1_CHANNEL_5   // GPIO33
#define BUTTON_PIN    GPIO_NUM_4       // Pause Button

/* Output pins (Munzir's) */
#define NEOPIXEL_PIN  GPIO_NUM_13      // NeoPixel ring data
#define LED_PIN       GPIO_NUM_25      // Shield phase LED
#define BUZZER_PIN    GPIO_NUM_18      // Piezo buzzer

/* OLED I2C pins (shared, OLED driver is pre-written) */
#define I2C_SDA       GPIO_NUM_21
#define I2C_SCL       GPIO_NUM_22
#define I2C_PORT      I2C_NUM_0
#define OLED_ADDR     0x3C

// Game Constants
#define SCREEN_W      128
#define SCREEN_H      64
#define SHIP_W        8     // Default ship width (changes with pot)
#define SHIP_H        6
#define MAX_OBSTACLES 6
#define NUM_PIXELS    16
#define FILTER_SIZE   5

// Task Priorities (Higher number higher priority)
#define PRIO_JOYSTICK     7
#define PRIO_SENSOR_CONSUMER  6 
#define PRIO_BOOST        5
#define PRIO_PAUSE        5
#define PRIO_OLED_RENDER  4
#define PRIO_SPAWNER      4
#define PRIO_POT          3
#define PRIO_NEOPIXEL     3
#define PRIO_LDR          2

// Event Group Bits
#define EVT_BOOST_ACTIVE   0b0001 // boost bitmask
#define EVT_NIGHTMARE_MODE 0b0010 // nightmare bitmask
#define EVT_GAME_OVER      0b0100 //game over bitmask
#define EVT_PAUSED         0b1000 //pause bitmask

// Game State
// Sensors provide the ship location based on the joystick, the shieldphase based on the pot, nightmaremode based on the LDR, boost based on the button
// Obstacles, score, speed, gameover is provided through the main 
// graphics renders all of the above using the OLED

typedef struct {
    int x, y, w, h;
    bool active;
} Obstacle;

typedef struct {
    // Ship state 
    int shipX;
    int shipY;
    int shipW;             // Dynamic width from potentiometer

    // Sensor-derived state 
    int shieldPhase;       // 0-255 from potentiometer
    int boostEnergy;       // 0-100, fuel gauge

    int ldrValue;          //we need the ldr value for the bar on oled screen

    // Game logic state (written by Rizwan's engine task)
    Obstacle obstacles[MAX_OBSTACLES];
    int score;
    int speed;
} GameState;

// Global Variables

extern volatile GameState game;
extern SemaphoreHandle_t gameMutex;
extern SemaphoreHandle_t boostSemaphore;         // Binary semaphore, for boost ISR 
extern SemaphoreHandle_t pauseSemaphore;         // Binary semaphore for pause ISR
extern QueueHandle_t sensorQueue;          // Queue for task_input 
extern EventGroupHandle_t gameEventGroup;  // Event group for the several flag mentioned above

// Sensor Item Types to be used as an identifier for the origin of any item to be inserted in queue
typedef enum {
    SENSOR_JOYSTICK,
    SENSOR_POT,
    SENSOR_LDR
} SensorType;

// Sensor item struct with the identifier of the sensor and the values obtained from the sensor
typedef struct {
    SensorType type;
    int value1;    // joystick: speedX, pot: rawW, ldr: ldrval
    int value2;    // joystick: speedY, unused by others
} SensorItem;

// Function prototypes

/* Rizwan's functions (main.c) */
void task_game_logic(void* arg);
void task_spawner(void* arg);

/* Munzir's functions (graphics.c) */
void task_oled_render(void* arg);
void task_neopixel(void* arg);

/* Ahmad's functions (sensors.c) */
void adc_init(void);
void sensors_init(void);
void task_joystick(void* arg);
void task_pot(void* arg);
void task_boost(void* arg);
void task_ldr(void* arg);
void task_pause(void* arg);
void task_sensor_consumer(void* arg);

/* OLED driver (pre-written, in oled.c)*/
void oled_init(void);            // Initialize I2C and SSD1306
void oled_flush(void);           // Push framebuffer to display
void fb_clear(void);             // Clear framebuffer
void fb_pixel(int x, int y, bool on);
void fb_rect(int x, int y, int w, int h);
void fb_vline(int x, int y, int len);
void fb_hline(int x, int y, int len);
void fb_frame(int x, int y, int w, int h);
void fb_char(int x, int y, char c);
void fb_string(int x, int y, const char* s);
void fb_ship(int x, int y, int w);

#endif /* GAME_H */
