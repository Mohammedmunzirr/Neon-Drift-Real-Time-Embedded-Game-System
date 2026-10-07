#include "game.h"

static const char* TAG = "NEONDRIFT";

// global variable definitions
// (These were declared as 'extern' in game.h, so they must be defined here)

volatile GameState game;
SemaphoreHandle_t gameMutex;
SemaphoreHandle_t boostSemaphore;
SemaphoreHandle_t pauseSemaphore;
QueueHandle_t sensorQueue;
EventGroupHandle_t gameEventGroup;

void app_main(void) {
    ESP_LOGI(TAG, "Booting NEON DRIFT...");

    //1. Creating FreeRTOS primitives before creating any tasks
    // If a task tries to take a mutex that isn't created yet, the ESP32 crashes.
    gameMutex = xSemaphoreCreateMutex();
    boostSemaphore = xSemaphoreCreateBinary();
    pauseSemaphore = xSemaphoreCreateBinary();
    sensorQueue = xQueueCreate(20, sizeof(SensorItem)); 
    gameEventGroup = xEventGroupCreate();

    //2. Setup initial game values
    xSemaphoreTake(gameMutex, portMAX_DELAY);
    game.shipX = SCREEN_W / 2;
    game.shipY = SCREEN_H - SHIP_H - 15; // Set near bottom, accounting for headers
    game.shipW = SHIP_W;
    game.shieldPhase = 0;
    game.boostEnergy = 100;
    game.score = 0;
    game.ldrValue = 4095; //right bar fully filled at the beginning
    game.speed = 2; // Initial fall speed 2 pixels/frame
    //game runs at 30 frames per second. 2 pixels x 30 frames =60 pixels/second
    //OLED screen is 128 x 64 pixels, so speed 2 means approx 1 second to fall

    for(int i = 0; i < MAX_OBSTACLES; i++) {
        game.obstacles[i].active = false;
        //All obstaces invisible initially
    }
    xSemaphoreGive(gameMutex);

    //3. Initialize Hardware Interfaces
    ESP_LOGI(TAG, "Initializing hardware (ADC, GPIO, OLED)...");
    adc_init();
    sensors_init();
    //oled is initialized inside init_outputs() which is called at the 
    //start of task_oled_render

    //4. Launch FreeRTOS Tasks
    ESP_LOGI(TAG, "Spawning Tasks across Core 0 and Core 1...");

    // CORE 0: Inputs & Game Logic 
    // syntax: function, name, stack size, params, priority, task handle, core ID
    
    // Ahmad's Sensor Producers
    xTaskCreatePinnedToCore(task_joystick, "Joystick", 2048, NULL, PRIO_JOYSTICK, NULL, 0); //Priority: 7
    xTaskCreatePinnedToCore(task_boost, "BoostBtn", 2048, NULL, PRIO_BOOST, NULL, 0);       //Priority: 5
    xTaskCreatePinnedToCore(task_pause, "PauseBtn", 2048, NULL, PRIO_PAUSE, NULL, 0);       //Priority: 5
    xTaskCreatePinnedToCore(task_pot, "PotDial", 2048, NULL, PRIO_POT, NULL, 0);            //Priority: 3
    xTaskCreatePinnedToCore(task_ldr, "LDRSensor", 2048, NULL, PRIO_LDR, NULL, 0);          //Priority: 2

    // Sensor Consumer
    xTaskCreatePinnedToCore(task_sensor_consumer, "SensConsumer", 4096, NULL, PRIO_SENSOR_CONSUMER, NULL, 0); //Priority: 6
    // Rizwan's Game Engine
    xTaskCreatePinnedToCore(task_game_logic, "GameLogic", 4096, NULL, PRIO_SENSOR_CONSUMER, NULL, 0);  //Priority: 6
    xTaskCreatePinnedToCore(task_spawner, "Spawner", 2048, NULL, PRIO_SPAWNER, NULL, 0);               //Priority: 4

    // CORE 1: Output & Display 
    // Munzir's Tasks 
    xTaskCreatePinnedToCore(task_oled_render, "OLEDRender", 8192, NULL, PRIO_OLED_RENDER, NULL, 1);    //Priority: 4
    xTaskCreatePinnedToCore(task_neopixel, "NeoPixel", 4096, NULL, PRIO_NEOPIXEL, NULL, 1);            //Priority: 3
    ESP_LOGI(TAG, "Initialization complete. Game loop running.");

     /*
    Priorities: 
    PRIO_JOYSTICK           7
    PRIO_SENSOR_CONSUMER    6 
    PRIO_BOOST              5
    PRIO_PAUSE              5
    PRIO_OLED_RENDER        4
    PRIO_SPAWNER            4
    PRIO_POT                3
    PRIO_NEOPIXEL           3
    PRIO_LDR                2
    */
}