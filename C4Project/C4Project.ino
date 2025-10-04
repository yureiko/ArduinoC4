#include <Arduino.h>
#include <pins_arduino.h>
#include <TM1637Display.h>

//Display pins
#define CLK 0
#define DIO 4

#define PLANT_DEFUSE_BUTTON 13
#define BUZZER_PIN          2

#define LED_BAR_0 21
#define LED_BAR_1 3
#define LED_BAR_2 1
#define LED_BAR_3 22
#define LED_BAR_4 23

#define LED_BOMB  19
#define LED_CT    18

#define ROUND_PLANT_TIME_SEC 60*30
#define ROUND_DEFUSE_TIME_SEC 60*10

typedef enum
{
  NONE,
  CT_WIN,
  TR_WIN
} eRoundWinner;

typedef enum
{
  IDLE,
  ROUND_START,
  READY_TO_PLANT,
  BOMB_PLANTED,
  ROUND_OVER,
} eRoundState;

typedef struct
{
  int iStartTime;
  bool bFreeze;
} tsTimerCommand;

typedef struct
{
  bool bLed0On;
  bool bLed1On;
  bool bLed2On;
  bool bLed3On;
  bool bLed4On;
} tsOutputLedBar;

typedef struct
{
  tsOutputLedBar sledBar;
  int iOutput;
  int iTimeOn;
  int iTimeOff;
  int iReps;
  bool iBeep;
} tsBuzzerCommand;

QueueHandle_t xTimerQueue;
QueueHandle_t xTimeLeftQueue;
QueueHandle_t xCommandQueue;
QueueHandle_t xBuzzerQueue;
QueueHandle_t xLedQueue;

TM1637Display display(CLK, DIO);

void setup() {
  display.setBrightness(7); // Set the brightness of the display (0-7)
  display.clear(); // Clear the display

  pinMode(PLANT_DEFUSE_BUTTON, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(LED_BAR_0, OUTPUT);
  pinMode(LED_BAR_1, OUTPUT);
  pinMode(LED_BAR_2, OUTPUT);
  pinMode(LED_BAR_3, OUTPUT);
  pinMode(LED_BAR_4, OUTPUT);
  pinMode(LED_BOMB , OUTPUT);
  pinMode(LED_CT   , OUTPUT);

  digitalWrite(LED_BAR_0, HIGH);
  digitalWrite(LED_BAR_1, HIGH);
  digitalWrite(LED_BAR_2, HIGH);
  digitalWrite(LED_BAR_3, HIGH);
  digitalWrite(LED_BAR_4, HIGH);

  digitalWrite(LED_BAR_0, LOW);
  delay(500);
  digitalWrite(LED_BAR_1, LOW);
  delay(500);
  digitalWrite(LED_BAR_2, LOW);
  delay(500);
  digitalWrite(LED_BAR_3, LOW);
  delay(500);
  digitalWrite(LED_BAR_4, LOW);
  delay(500);
  digitalWrite(LED_BOMB , LOW);
  digitalWrite(LED_CT   , LOW);

  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
  delay(50);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
  delay(50);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);

  digitalWrite(LED_BAR_0, HIGH);
  delay(500);
  digitalWrite(LED_BAR_1, HIGH);
  delay(500);
  digitalWrite(LED_BAR_2, HIGH);
  delay(500);
  digitalWrite(LED_BAR_3, HIGH);
  delay(500);
  digitalWrite(LED_BAR_4, HIGH);
  delay(500);
  digitalWrite(LED_BOMB , HIGH);
  digitalWrite(LED_CT   , HIGH);

  xTimerQueue = xQueueCreate(1, sizeof(tsTimerCommand));
  xTimeLeftQueue = xQueueCreate(1, sizeof(unsigned long));
  xCommandQueue = xQueueCreate(1, sizeof(int));
  xBuzzerQueue = xQueueCreate(1, sizeof(tsBuzzerCommand));
  xLedQueue = xQueueCreate(1, sizeof(xLedQueue));

   // Task to run forever
  xTaskCreatePinnedToCore(        // Use xTaskCreate() in vanilla FreeRTOS
              vRoundTask,         // Function to be called
              "vRoundTask",       // Name of task
              2048,                // Stack size (bytes in ESP32, words in FreeRTOS)
              NULL,               // Parameter to pass to function
              1,                  // Task priority (0 to to configMAX_PRIORITIES - 1)
              NULL,               // Task handle
              0);                 // Run on one core for demo purposes (ESP32 only)

  xTaskCreatePinnedToCore(        // Use xTaskCreate() in vanilla FreeRTOS
              vTimerTask,         // Function to be called
              "vTimerTask",       // Name of task
              1024,                // Stack size (bytes in ESP32, words in FreeRTOS)
              NULL,               // Parameter to pass to function
              1,                  // Task priority (0 to to configMAX_PRIORITIES - 1)
              NULL,               // Task handle
              0);                 // Run on one core for demo purposes (ESP32 only)

  xTaskCreatePinnedToCore(        // Use xTaskCreate() in vanilla FreeRTOS
              vButtonTask,         // Function to be called
              "vButtonTask",       // Name of task
              1024,                // Stack size (bytes in ESP32, words in FreeRTOS)
              NULL,               // Parameter to pass to function
              1,                  // Task priority (0 to to configMAX_PRIORITIES - 1)
              NULL,               // Task handle
              0);                 // Run on one core for demo purposes (ESP32 only)
  xTaskCreatePinnedToCore(        // Use xTaskCreate() in vanilla FreeRTOS
              vBuzzerTask,         // Function to be called
              "vBuzzerTask",       // Name of task
              1024,                // Stack size (bytes in ESP32, words in FreeRTOS)
              NULL,               // Parameter to pass to function
              1,                  // Task priority (0 to to configMAX_PRIORITIES - 1)
              NULL,               // Task handle
              1);                 // Run on one core for demo purposes (ESP32 only)
}

void vRoundTask(void *pvParameters)
{
  
  tsTimerCommand sTimerCommand = {};
  tsBuzzerCommand sBuzzerCommand = {{false,false,false,false,false},0, 0, 0, 0};

  int iButtonPressedTime = 0;
  int iPlantPressTime = 4000;
  int iDefusePressTime = 10000;

  unsigned long remainingTime = ROUND_PLANT_TIME_SEC*100;
  unsigned long nextBeepTime = 0;
  unsigned long nextBlinkTime = 0;

  bool bButtonReleaseNeed = false;
  bool bSoundInExecution = false;

  sTimerCommand.iStartTime = ROUND_PLANT_TIME_SEC;
  sTimerCommand.bFreeze = true;
  xQueueSend(xTimerQueue, &sTimerCommand, 0);

  eRoundState eState = eRoundState::IDLE;
  eRoundWinner eWinner = eRoundWinner::NONE;

  while(1)
  {
    xQueueReceive(xTimeLeftQueue, &remainingTime, pdMS_TO_TICKS(0));

    if(xQueueReceive(xCommandQueue, &iButtonPressedTime, pdMS_TO_TICKS(0)) == true)
    {
      if(bButtonReleaseNeed && iButtonPressedTime == 0)
      {
        bButtonReleaseNeed = false;
      }
      if(bSoundInExecution && iButtonPressedTime == 0)
      {
        bSoundInExecution = false;
      }
    }

    switch(eState)
    {
      default:
      case IDLE:
      {
        if(!bButtonReleaseNeed && iButtonPressedTime >= iPlantPressTime)
        {
          bButtonReleaseNeed = true;
          sTimerCommand.iStartTime = ROUND_PLANT_TIME_SEC;
          sTimerCommand.bFreeze = true;
          xQueueSend(xTimerQueue, &sTimerCommand, 0);
          vTaskDelay(pdMS_TO_TICKS(100));

          sBuzzerCommand.sledBar.bLed0On = false;
          sBuzzerCommand.sledBar.bLed1On = false;
          sBuzzerCommand.sledBar.bLed2On = false;
          sBuzzerCommand.sledBar.bLed3On = false;
          sBuzzerCommand.sledBar.bLed4On = false;
          sBuzzerCommand.iOutput = LED_CT;
          sBuzzerCommand.iTimeOn = 500;
          sBuzzerCommand.iTimeOff = 1000;
          sBuzzerCommand.iReps = 3;
          sBuzzerCommand.iBeep = true; 
          xQueueSend(xBuzzerQueue, &sBuzzerCommand, 0);
          vTaskDelay(pdMS_TO_TICKS(3000));
          eState = eRoundState::ROUND_START;
        }
        break;
      }
      case ROUND_START:
      {
        sTimerCommand.iStartTime = ROUND_PLANT_TIME_SEC;
        sTimerCommand.bFreeze = false;
        xQueueSend(xTimerQueue, &sTimerCommand, 0);
        vTaskDelay(pdMS_TO_TICKS(100));

        nextBlinkTime = remainingTime;
        eState = eRoundState::READY_TO_PLANT;
        break;
      }
      case READY_TO_PLANT:
      {
        if(remainingTime == 0)
        {
          eWinner = CT_WIN;
          eState = eRoundState::ROUND_OVER;
        }
        else if (remainingTime <= nextBlinkTime)
        {
          bSoundInExecution = true;

          sBuzzerCommand.iOutput = LED_BOMB;
          sBuzzerCommand.iTimeOn = 100;
          sBuzzerCommand.iTimeOff = 25;
          sBuzzerCommand.iReps = 1;   
          sBuzzerCommand.iBeep = false;  
          xQueueSend(xBuzzerQueue, &sBuzzerCommand, 0);

          if(remainingTime > 5)
          {
            nextBlinkTime = remainingTime - 5;
          }
        }
        else if(!bButtonReleaseNeed)
        {
          sBuzzerCommand.sledBar.bLed0On = iButtonPressedTime > 500;
          sBuzzerCommand.sledBar.bLed1On = iButtonPressedTime >= iPlantPressTime * 0.25;
          sBuzzerCommand.sledBar.bLed2On = iButtonPressedTime >= iPlantPressTime * 0.50;
          sBuzzerCommand.sledBar.bLed3On = iButtonPressedTime >= iPlantPressTime * 0.75;
          sBuzzerCommand.sledBar.bLed4On = iButtonPressedTime >= iPlantPressTime;

          if(!bSoundInExecution && iButtonPressedTime > 100 && iButtonPressedTime < 500)
          {
            bSoundInExecution = true;

            sBuzzerCommand.iOutput = LED_BOMB;
            sBuzzerCommand.iTimeOn = 100;
            sBuzzerCommand.iTimeOff = 25;
            sBuzzerCommand.iReps = 2;
            sBuzzerCommand.iBeep = true;  
          }
          else
          {
            sBuzzerCommand.iOutput = LED_BOMB;
            sBuzzerCommand.iTimeOn = 100;
            sBuzzerCommand.iTimeOff = 25;
            sBuzzerCommand.iReps = 0;
            sBuzzerCommand.iBeep = false;  
          }
          xQueueSend(xBuzzerQueue, &sBuzzerCommand, 0);
        }
        
        if(!bButtonReleaseNeed && iButtonPressedTime >= iPlantPressTime)
        {
          bButtonReleaseNeed = true;

          sTimerCommand.iStartTime = ROUND_DEFUSE_TIME_SEC;
          sTimerCommand.bFreeze = false;
          xQueueSend(xTimerQueue, &sTimerCommand, 0);

          nextBeepTime = remainingTime;

          eState = eRoundState::BOMB_PLANTED;
        }
        
        break;
      }
      case BOMB_PLANTED:
      {
        if(remainingTime == 0)
        {
          eWinner = TR_WIN;
          eState = eRoundState::ROUND_OVER;
        }
        else if (remainingTime <= nextBeepTime)
        {
          nextBeepTime = (unsigned long)(0.1 + 0.98 * (double)remainingTime);
          sBuzzerCommand.iOutput = LED_BOMB;
          sBuzzerCommand.iTimeOn = 99;
          sBuzzerCommand.iTimeOff = 1;
          sBuzzerCommand.iReps = 1;
          sBuzzerCommand.iBeep = true;  
          xQueueSend(xBuzzerQueue, &sBuzzerCommand, 0);
        }
        else if(!bButtonReleaseNeed)
        {
          sBuzzerCommand.sledBar.bLed0On = !(iButtonPressedTime > 500);
          sBuzzerCommand.sledBar.bLed1On = !(iButtonPressedTime >= iDefusePressTime * 0.25);
          sBuzzerCommand.sledBar.bLed2On = !(iButtonPressedTime >= iDefusePressTime * 0.50);
          sBuzzerCommand.sledBar.bLed3On = !(iButtonPressedTime >= iDefusePressTime * 0.75);
          sBuzzerCommand.sledBar.bLed4On = !(iButtonPressedTime >= iDefusePressTime);

          if(!bSoundInExecution && iButtonPressedTime > 100 && iButtonPressedTime < 500)
          {
            bSoundInExecution = true;

            sBuzzerCommand.iOutput = LED_CT;
            sBuzzerCommand.iTimeOn = 100;
            sBuzzerCommand.iTimeOff = 25;
            sBuzzerCommand.iReps = 2;
            sBuzzerCommand.iBeep = true;   

          }
          else
          {
            sBuzzerCommand.iOutput = LED_BOMB;
            sBuzzerCommand.iTimeOn = 100;
            sBuzzerCommand.iTimeOff = 25;
            sBuzzerCommand.iReps = 0;
            sBuzzerCommand.iBeep = false;  
          }
          xQueueSend(xBuzzerQueue, &sBuzzerCommand, 0);
        }
        if(!bButtonReleaseNeed && iButtonPressedTime >= iDefusePressTime)
        {
          bButtonReleaseNeed = true;

          sTimerCommand.iStartTime = 0;
          sTimerCommand.bFreeze = false;
          xQueueSend(xTimerQueue, &sTimerCommand, 0);

          sBuzzerCommand.iOutput = LED_BOMB;
          sBuzzerCommand.iTimeOn = 100;
          sBuzzerCommand.iTimeOff = 25;
          sBuzzerCommand.iReps = 0;
          sBuzzerCommand.iBeep = false;  
          xQueueSend(xBuzzerQueue, &sBuzzerCommand, 0);

          eWinner = CT_WIN;
          eState = eRoundState::ROUND_OVER;
        }

        break;
      }
      case ROUND_OVER:
      {
        if(!bButtonReleaseNeed && iButtonPressedTime >= iPlantPressTime)
        {
          bButtonReleaseNeed = true;

          sTimerCommand.iStartTime = ROUND_PLANT_TIME_SEC;
          sTimerCommand.bFreeze = true;
          xQueueSend(xTimerQueue, &sTimerCommand, 0);
          vTaskDelay(pdMS_TO_TICKS(100));
          eWinner = NONE;
          eState = eRoundState::IDLE;
        }
        if (eWinner == CT_WIN)
        {
          sBuzzerCommand.sledBar.bLed0On = false;
          sBuzzerCommand.sledBar.bLed1On = false;
          sBuzzerCommand.sledBar.bLed2On = false;
          sBuzzerCommand.sledBar.bLed3On = false;
          sBuzzerCommand.sledBar.bLed4On = false;
          sBuzzerCommand.iOutput = LED_CT;
          sBuzzerCommand.iTimeOn = 1000;
          sBuzzerCommand.iTimeOff = 500;
          sBuzzerCommand.iReps = 10;
          sBuzzerCommand.iBeep = true;
          xQueueSend(xBuzzerQueue, &sBuzzerCommand, 0);
          vTaskDelay(pdMS_TO_TICKS(500));
          
        }
        else if (eWinner == TR_WIN)
        {
          sBuzzerCommand.sledBar.bLed0On = true;
          sBuzzerCommand.sledBar.bLed1On = true;
          sBuzzerCommand.sledBar.bLed2On = true;
          sBuzzerCommand.sledBar.bLed3On = true;
          sBuzzerCommand.sledBar.bLed4On = true;
          sBuzzerCommand.iOutput = LED_BOMB;
          sBuzzerCommand.iTimeOn = 25;
          sBuzzerCommand.iTimeOff = 25;
          sBuzzerCommand.iReps = 100;
          sBuzzerCommand.iBeep = true;  
          xQueueSend(xBuzzerQueue, &sBuzzerCommand, 0);
          vTaskDelay(pdMS_TO_TICKS(500));
          
        }
        break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void vTimerTask(void *pvParameters) 
{
  unsigned long startTime = 0;
  unsigned long currentTime;
  unsigned long elapsedTime;

  unsigned long remainingTime = 0;

  tsTimerCommand sTimerCommand = {};

  while(1)
  { 
    // Start coutdown
    if(xQueueReceive(xTimerQueue, &sTimerCommand, pdMS_TO_TICKS(0)) == true)
    {
      startTime = millis();
      remainingTime = sTimerCommand.iStartTime;
    }

    if(startTime == 0)
    {
      display.showNumberDecEx(0, 0b01000000, true); // Display "00:00"
    }
    else
    {
      currentTime = millis(); // Get the current time
      elapsedTime = (currentTime - startTime) / 1000; // Calculate elapsed time in seconds
    
      if (!sTimerCommand.bFreeze && elapsedTime <= sTimerCommand.iStartTime) 
      {
        remainingTime = sTimerCommand.iStartTime - elapsedTime;
    
        // Display remaining time in Minutes:Seconds format
        unsigned int minutes = remainingTime / 60;
        unsigned int seconds = remainingTime % 60;
        display.showNumberDecEx(minutes * 100 + seconds, 0b01000000, true);
      }
      else if(sTimerCommand.bFreeze)
      {
        unsigned int minutes = sTimerCommand.iStartTime / 60;
        unsigned int seconds = sTimerCommand.iStartTime % 60;
        display.showNumberDecEx(minutes * 100 + seconds, 0b01000000, true);
      }
      else
      {
        remainingTime = startTime = 0;
      }
    }

    xQueueSend(xTimeLeftQueue, &remainingTime, 0);

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void vButtonTask(void *pvParameters)
{
  int iButtonState = 0;
  int iButtonLastState = 0;
  int iButtonTimePressed = 0;

  while (1)
  {
    iButtonState = digitalRead(PLANT_DEFUSE_BUTTON);

    if(iButtonState == LOW && iButtonState == iButtonLastState)
    {
      iButtonTimePressed += 100;
    }
    else
    {
      iButtonTimePressed = 0;
    }

    iButtonLastState = iButtonState;

    xQueueSend(xCommandQueue, &iButtonTimePressed, 0);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void vBuzzerTask(void *pvParameters)
{
  tsBuzzerCommand sBuzzerCommand = {0, 0, 0, 0, 0};

  while (1)
  {
    xQueueReceive(xBuzzerQueue, &sBuzzerCommand, pdMS_TO_TICKS(10));

    digitalWrite(LED_BAR_0, !(sBuzzerCommand.sledBar.bLed0On));
    digitalWrite(LED_BAR_1, !(sBuzzerCommand.sledBar.bLed1On));
    digitalWrite(LED_BAR_2, !(sBuzzerCommand.sledBar.bLed2On));
    digitalWrite(LED_BAR_3, !(sBuzzerCommand.sledBar.bLed3On));
    digitalWrite(LED_BAR_4, !(sBuzzerCommand.sledBar.bLed4On));

    while(sBuzzerCommand.iReps > 0)
    {
      digitalWrite(sBuzzerCommand.iOutput, LOW);
      if(sBuzzerCommand.iBeep) 
      {
        digitalWrite(BUZZER_PIN, HIGH);
      }
      vTaskDelay(pdMS_TO_TICKS(sBuzzerCommand.iTimeOn));
      digitalWrite(sBuzzerCommand.iOutput, HIGH);
      if(sBuzzerCommand.iBeep) 
      {
        digitalWrite(BUZZER_PIN, LOW);
      }
      vTaskDelay(pdMS_TO_TICKS(sBuzzerCommand.iTimeOff));

      sBuzzerCommand.iReps--;
    }
    vTaskDelay(1);
  }
}

void loop()
{
}
