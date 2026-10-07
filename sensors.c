//sensors.c - Ahmad
#include "game.h"
#include "driver/adc.h"

static int xbuf[FILTER_SIZE] = {0};
static int ybuf[FILTER_SIZE] = {0};
static int filterIdx = 0;

void adc_init(void){
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(JOY_X_PIN, ADC_ATTEN_DB_12); // Configuring ADC Channel for joystick on x axis
    adc1_config_channel_atten(JOY_Y_PIN, ADC_ATTEN_DB_12); // Configuring ADC Channel for joystick on y axis
    adc1_config_channel_atten(POT_PIN, ADC_ATTEN_DB_12); // Configuring ADC for pot
    adc1_config_channel_atten(LDR_PIN, ADC_ATTEN_DB_12); // Configuring ADC for LDR
}

static int filtered_adc(adc1_channel_t ch, int* buf) {
    int sum=0;
    
    buf[filterIdx % FILTER_SIZE] = adc1_get_raw(ch); // Store the ADC reading in the circular buffer
    
     for(int i=0;i<FILTER_SIZE;i++){
        sum += buf[i];  // Sum all values in the circular buffer
     }
    return sum/FILTER_SIZE; //Find the average value of all parameters in the circular buffer
}
// The above function ensures smoothness in movement, as taking one ADC reading at a time will cause abruptness and sudden movements
// getting an average would smoothen out the movements

// Boost Button ISR
 static void IRAM_ATTR boost_isr(void* arg) // Defining the boost ISR, based on IRAM safe interrupt handlers, where IRAM has low latency and avoid cache misses
 {
    xSemaphoreGiveFromISR(boostSemaphore, NULL); // giving the semaphore in the ISR to be used when actually boosting
}

 // Pause ISR
  static void IRAM_ATTR pause_isr(void* arg) // Defining the pause ISR, based on IRAM safe interrupt handlers, where IRAM has low latency and avoid cache misses
 {
    xSemaphoreGiveFromISR(pauseSemaphore, NULL); // giving the semaphore in the ISR to be used when actually pausing
 }

 void sensors_init(void){
    gpio_config_t pause_cfg = {0};
    pause_cfg.pin_bit_mask = (1ULL << BUTTON_PIN); // Configuring the bit mask, choosing a 64 bit (unsigned long long)
                                                // Taking the BUTTON_PIN and shifting the 1 to the left by BUTTON_PIN positions
    pause_cfg.mode = GPIO_MODE_INPUT; // Set the button pin to input
    pause_cfg.pull_up_en = GPIO_PULLUP_ENABLE; // Set the button to pull up
    pause_cfg.intr_type = GPIO_INTR_NEGEDGE; // Set the interrupt to negative edge

    gpio_config_t boost_cfg = {0};
    boost_cfg.pin_bit_mask = (1ULL << JOY_SEL_PIN); // Configuring the bit mask, choosing a 64 bit (unsigned long long)
                                                // Taking the BUTTON_PIN and shifting the 1 to the left by JOY_SEL_PIN positions
    boost_cfg.mode = GPIO_MODE_INPUT; // Set the button pin to input
    boost_cfg.pull_up_en = GPIO_PULLUP_ENABLE; // Set the button to pull up
    boost_cfg.intr_type = GPIO_INTR_NEGEDGE; // Set the interrupt to negative edge

    gpio_config(&pause_cfg);
    gpio_config(&boost_cfg);

    gpio_install_isr_service(0); // Installing interrupt handler for the GPIO, 0 is passed as no flags defined
    gpio_isr_handler_add(BUTTON_PIN, pause_isr, NULL); // Setting up the pause isr handler
    gpio_isr_handler_add(JOY_SEL_PIN, boost_isr, NULL); // Setting up the boost isr handler

 }

 void task_joystick(void* arg){
    int rawX;
    int rawY;
    int dx;
    int dy;
    int speedX;
    int speedY;

    while (1) {
        
        rawX = filtered_adc(JOY_X_PIN, xbuf);
        rawY = filtered_adc(JOY_Y_PIN, ybuf);
        filterIdx++;

        dx = 2048 - rawX; // Subtract the center value
        if (abs(dx) < 300) { // Check for dead zone of joystick (assumed as 300), as the it would not read an actual zero in real life
            speedX = 0; // If the change (dx) is less than 300, this is just stick drift and we ignore it
        }
        else if (dx>0) { // If the dx is greater than zero, so movement in the positive axis, to the right
            speedX = 1 + (dx-300) / 600; // Get rid of dead zone and divide by 600 to scale the speed, such that the ship doesnt jump quickly
        }
        else { //if dx is less than zero, then it is shift to the left
            speedX = -(1 + (-dx - 300) / 600);
        }

        dy = 2048 - rawY; // Subtract the center value
        if (abs(dy) < 300) { // Check for dead zone of joystick (assumed as 300), as the it would not read an actual zero in real life
            speedY = 0;
        }
        else if (dy>0) { // If the dy is greater than zero, so movement in the positive axis, upwards
            speedY = 1 + (dy-300) / 600;// Get rid of dead zone and divide by 600 to scale the speed, such that the ship doesnt jump quickly
        }
        else { // dy less than zero, so downwards
            speedY = -(1 + (-dy - 300) / 600);
        }

        SensorItem joystickItem; // Create Sensor Item, to be sent in queue (for joystick)
        joystickItem.type = SENSOR_JOYSTICK; // Set item type to joystick
        joystickItem.value1 = speedX;
        joystickItem.value2 = speedY;
        xQueueSend(sensorQueue, &joystickItem, portMAX_DELAY); // send item into queue

        vTaskDelay(pdMS_TO_TICKS(30));
    }
 }

void task_pot(void* arg){
    int rawW;
    int shipW;
    int shieldPhase;
    while(1){
        rawW = adc1_get_raw(POT_PIN);
        shipW = 6 + (rawW * 14) / 4095; // Changing the width of the ship based on the pot readings, Design choice: ship width betwen 6-20
                                            
        shieldPhase = (rawW * 255) / 4095; // Changing the shieldPhase according to pot readings, Design choice: Limiting shield phase between 0 and 255
        
        SensorItem potItem; // Create Sensor Item, to be sent in queue (for pot)
        potItem.type = SENSOR_POT; // Set item type to pot
        potItem.value1 = shipW;
        potItem.value2 = shieldPhase;
        xQueueSend(sensorQueue, &potItem, portMAX_DELAY); // send item into queue

        vTaskDelay(pdMS_TO_TICKS(150)); // Enough delay for the turn of the knob
    }
}

void task_ldr(void* arg){
    int ldrVal;

    while(1) {
        ldrVal = adc1_get_raw(LDR_PIN);

        SensorItem ldrItem; // Create Sensor Item, to be sent in queue (for LDR)
        ldrItem.type = SENSOR_LDR; // Set item type to LDR
        ldrItem.value1 = ldrVal;
        ldrItem.value2 = 0; // sending 0 because no 2nd reading
        xQueueSend(sensorQueue, &ldrItem, portMAX_DELAY); // send item into queue

        vTaskDelay(pdMS_TO_TICKS(300));


    }
}

void task_boost(void* arg) {
    while(1) {

        xSemaphoreTake(boostSemaphore, portMAX_DELAY); // Block anymore boosts until ISR gives semaphore

        EventBits_t gameBits = xEventGroupGetBits(gameEventGroup); // Saving the event group in var for comparison
        if (gameBits & EVT_GAME_OVER || gameBits & EVT_PAUSED) { // Bitwise AND with the MASKS in game.h in this case: 0b0100 (gameover) or 0b1000 (pause)
            while (gpio_get_level(JOY_SEL_PIN) == 0)   // Button debouncing, where as the button is held, we do nothing and do not boost because we are paused or in game over
                vTaskDelay(pdMS_TO_TICKS(20));
            vTaskDelay(pdMS_TO_TICKS(50));
            xSemaphoreTake(boostSemaphore, 0); // here we take the semaphore to clear the semaphore value, bc if we are paused or game over we can not boost
            continue;
        }

        while (gpio_get_level(JOY_SEL_PIN) == 0) {

            EventBits_t gameBits = xEventGroupGetBits(gameEventGroup); // Saving the event group in var for comparison
            if (gameBits & EVT_GAME_OVER || gameBits & EVT_PAUSED)
                break;
            xSemaphoreTake(gameMutex, portMAX_DELAY); // Take mutex

        if (game.boostEnergy > 0) {
            xEventGroupSetBits(gameEventGroup, EVT_BOOST_ACTIVE);
            game.boostEnergy -= 8; // Draining boost energy by 8
        } else {
            xEventGroupClearBits(gameEventGroup, EVT_BOOST_ACTIVE); // Clear boost
            xSemaphoreGive(gameMutex); //give mutex, as boost finished
            break;
        }

        xSemaphoreGive(gameMutex); //give mutex
        vTaskDelay(pdMS_TO_TICKS(100));
    }
        xEventGroupClearBits(gameEventGroup, EVT_BOOST_ACTIVE); // Clear boost, if joystick select is released
        xSemaphoreTake(gameMutex, portMAX_DELAY); // Take mutex
        game.boostEnergy = 100; // Recharge boost

        xSemaphoreGive(gameMutex); //give mutex

        while (gpio_get_level(JOY_SEL_PIN) == 0) 
            vTaskDelay(pdMS_TO_TICKS(20));  // Button debouncing, where as the button is held, we do nothing until the button is realeased
        vTaskDelay(pdMS_TO_TICKS(50));
        xSemaphoreTake(boostSemaphore, 0);
    }
}

void task_pause(void* arg) {
    while(1) {
        xSemaphoreTake(pauseSemaphore, portMAX_DELAY); // block until ISR gives the semaphore

        EventBits_t gameBits = xEventGroupGetBits(gameEventGroup); // Saving the event group in var for comparison
        if (gameBits & EVT_GAME_OVER) { // Bitwise AND with the MASKS in game.h in this case: 0b0100 (gameover)
            // Button debouncing, where as the button is held, we do nothing and ignore this button press because we can't pause if game is over
            while (gpio_get_level(BUTTON_PIN) == 0) 
                vTaskDelay(pdMS_TO_TICKS(20));
            vTaskDelay(pdMS_TO_TICKS(50));
            xSemaphoreTake(pauseSemaphore, 0); // to clear out the semaphore give from the ISR directly, to avoid semaphore buildup when pausing is not actually possible
            continue;
        }

        // Toggle once on the press edge
        if (gameBits & EVT_PAUSED) {
            xEventGroupClearBits(gameEventGroup, EVT_PAUSED); // Clear pause bits, if already paused
        } else {
            xEventGroupSetBits(gameEventGroup, EVT_PAUSED); // set pause bits, if not paused
        }

        while (gpio_get_level(BUTTON_PIN) == 0) // wait for user button to release the button
            vTaskDelay(pdMS_TO_TICKS(20));
        vTaskDelay(pdMS_TO_TICKS(50));
        xSemaphoreTake(pauseSemaphore, 0); // here we want to get rid of any false pause, by getting rid of the give semapgore from the ISR, making the pause ready for a new press
    }
}

void task_sensor_consumer(void* arg) {

    SensorItem item;

    while (1) {
        
        if (xQueueReceive(sensorQueue, &item, portMAX_DELAY) == pdTRUE)
        {
            xSemaphoreTake(gameMutex, portMAX_DELAY); // Here we take the mutex to avoid any race conditions between tasks

            if (item.type == SENSOR_JOYSTICK)
            {
           
            int speedmultiplier = 1;
             // If boost is active, we will double the ship speed it physically moves faster
            if (xEventGroupGetBits(gameEventGroup) & EVT_BOOST_ACTIVE) {
                speedmultiplier = 2;
            }
            game.shipX += item.value1 * speedmultiplier;
            game.shipY += item.value2 * speedmultiplier;

            // Clamping the ship mvmt, to avoid ship going off the screen from the left side, this is done by adding boundaries of 2 on the x axis on the OLED
            if (game.shipX <2) {
                game.shipX = 2;
            }
            if (game.shipX > SCREEN_W-game.shipW-2) { // Also avoiding the ship going off from the right side
                game.shipX = SCREEN_W-game.shipW-2;
            }

            if (game.shipY <12) { // Also avoiding ship going from the top, 12 used because of the header we added on the OLED
                game.shipY = 12;
            }
            if (game.shipY > SCREEN_H-SHIP_H-12) { // Avoiding ship going from the buttom
                game.shipY = SCREEN_H-SHIP_H-12;
            }
         
        }
        else if (item.type == SENSOR_POT) {
            game.shipW = item.value1; // Change width of ship based on scaled pot reading from pot task
            game.shieldPhase = item.value2; // change shieldphase based on scaled pot reading
        }
        else if(item.type == SENSOR_LDR) {
        
            game.ldrValue = 4095 - item.value1; //we need this value to display on oled screen
            //invert the value so High Lux = High Value for the OLED bar

            if (item.value1 < 1000) {
                xEventGroupSetBits(gameEventGroup, EVT_NIGHTMARE_MODE); // If the LDR reading is less than 1000 (own set threshold), then we set nighmare mode on event group
            } else {
                xEventGroupClearBits(gameEventGroup, EVT_NIGHTMARE_MODE); // if LDR above threshold, we clear eventgroup bits of nightmare mode
            }
        }
        xSemaphoreGive(gameMutex);

    }
    }
}
