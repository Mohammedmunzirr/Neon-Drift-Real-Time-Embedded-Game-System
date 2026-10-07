//game_engine.c - Rizwan

#include "game.h"
//This file handles the physics, moving the obstacles, checking for collisions, 
//and spawning new obstacles

// We need a width and height for our obstacles (since they aren't in the struct)
#define OBS_W 10
#define OBS_H 5


// Task: Game Logic (Physics & Collisions)

void task_game_logic(void* arg) {
    // We use this to maintain a steady framerate (around 30 FPS)
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        // Sleep until exactly 33 ticks (ms) have passed
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(33));
        //Not normal delay (don't wait 33ms after game logic is done)
        //irrespective of whether calculation is done or not, wake up after 33ms

        // Get the current flags (pause, game over, etc.)
        EventBits_t bits = xEventGroupGetBits(gameEventGroup);
        
        // If the game is paused or you crashed, freeze the physics
        if ((bits & EVT_PAUSED) || (bits & EVT_GAME_OVER)) {
            continue; 
        }

        // Lock the game state so the sensor task or OLED task don't mess 
        // with the variables while we are doing math
        xSemaphoreTake(gameMutex, portMAX_DELAY);

        // Base falling speed
        int fall_speed = game.speed; 
        if (fall_speed <= 0) fall_speed = 2; // Default fallback speed
        
        // Apply modifiers based on sensor events
        if (bits & EVT_NIGHTMARE_MODE) {
            fall_speed += 2; // Darkness makes them fall faster!
        }
        if (bits & EVT_BOOST_ACTIVE) {
            fall_speed += 3; // Holding boost speeds up the game
        }
        //game.speed = fall_speed;
        //we can not update game.speed as it is the baseline we return to

        // Update all obstacles
        for (int i = 0; i < MAX_OBSTACLES; i++) {
            if (game.obstacles[i].active) {
                // Move the obstacle down
                game.obstacles[i].y += fall_speed;

                // Box Collision check (AABB) - comparing ship coordinates vs obstacle coordinates
                //Axis-Aligned Bounding Box collision detection

                bool hitX = (game.shipX < game.obstacles[i].x + OBS_W) && 
                            (game.shipX + game.shipW > game.obstacles[i].x);
                bool hitY = (game.shipY < game.obstacles[i].y + OBS_H) && 
                            (game.shipY + SHIP_H > game.obstacles[i].y);
                
                //          |     |  obstacle
                //       |     |     ship
                
                if (hitX && hitY) {
                    // Collision detected! Set the game over flag
                    xEventGroupSetBits(gameEventGroup, EVT_GAME_OVER);
                }

                // If it goes off the bottom of the screen, we survived it
                if (game.obstacles[i].y > SCREEN_H) {
                    game.obstacles[i].active = false; // Recycle it
                    game.score += 10;                 // Points given
                }
            }
        }

        // Give the lock back
        xSemaphoreGive(gameMutex);
    }
}


// Task: Spawner

void task_spawner(void* arg) {
    while (1) {
        EventBits_t bits = xEventGroupGetBits(gameEventGroup);
        
        // Don't spawn boxes if paused or dead
        if ((bits & EVT_PAUSED) || (bits & EVT_GAME_OVER)) {
            vTaskDelay(pdMS_TO_TICKS(200)); 
            continue;
        }

        // Lock before editing the obstacle array
        xSemaphoreTake(gameMutex, portMAX_DELAY);
        
        // Find an inactive slot and spawn a new box
        for (int i = 0; i < MAX_OBSTACLES; i++) {
            if (!game.obstacles[i].active) {
                game.obstacles[i].active = true;
                // Random X position, making sure it doesn't hang off the right edge
                game.obstacles[i].x = esp_random() % (SCREEN_W - OBS_W); 
                game.obstacles[i].y = -OBS_H; // Start slightly above the screen
                game.obstacles[i].w = OBS_W;
                game.obstacles[i].h = OBS_H;
                break; // Only spawn one per loop
            }
        }
        
        xSemaphoreGive(gameMutex);

        // Figure out how long to wait before spawning the next one
        int delay_ms = 1500; // Normal rate
        if (bits & EVT_NIGHTMARE_MODE) delay_ms = 800; //Spawn more often
        if (bits & EVT_BOOST_ACTIVE) delay_ms = 500;   //Spawn very quickly
        
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}