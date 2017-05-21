/*
 * TrackCam.c
 *
 *  Created on: 2017/04/22
 *      Author: opiopan@gmail.com
 */

#include "project.h"
#include "TrackCam.h"
#include "Servo.h"
#include "LED.h"
#include "HostComm.h"

#include <string.h>

/*
 extern ADC_HandleTypeDef hadc;
 extern SPI_HandleTypeDef hspi1;
 extern TIM_HandleTypeDef htim1;
 extern TIM_HandleTypeDef htim2;
 */

extern void Error_Handler(void);

static TrackCamContext context;

static ServoHandle hservo;
static LEDHandle hled;
static HostCommHandle hcomm;

volatile uint8_t lastcmd = 0xff;
volatile int cmdcount = 0;

void trackCamMain(TrackCamContext* c)
{
    context = *c;

    if (initLED(&hled, context.htimLed, GPIOB, GPIO_PIN_0, GPIOB, GPIO_PIN_1)
            != HAL_OK) {
        Error_Handler();
    }
    if (initServo(&hservo, context.htimMotor, context.hadc) != HAL_OK) {
        Error_Handler();
    }
    if (initHostComm(&hcomm, &hled, &hservo) != HAL_OK) {
        Error_Handler();
    }

    // Warm up device
    startLED(&hled);
    HAL_Delay(500);
    LED_CONFIG(&hled, LED_SEQ_LEVEL_USER).type[LED_BLUE] = LED_SEQ_TYPE_OFF;
    LED_CONFIG(&hled, LED_SEQ_LEVEL_USER).type[LED_RED] = LED_SEQ_TYPE_ON;
    commitLEDConfig(&hled, LED_SEQ_LEVEL_USER);

    startServo(&hservo);
    LED_CONFIG(&hled, LED_SEQ_LEVEL_MASTER).type[LED_BLUE] =
            LED_SEQ_TYPE_SERVO_IDLE;
    LED_CONFIG(&hled, LED_SEQ_LEVEL_MASTER).type[LED_RED] =
            LED_SEQ_TYPE_SERVO_IDLE;
    commitLEDConfig(&hled, LED_SEQ_LEVEL_MASTER);

    while (1) {
        uint8_t magic = TRACKCAM_MAGIC_CMD;
        uint8_t cmd;
        static uint8_t rxbuff[64];
        static uint8_t txbuff[64 + 4];

        int rc = HAL_SPI_TransmitReceive(context.hspi, &magic, &cmd, 1,
                HAL_MAX_DELAY);
        if (rc != HAL_OK) {
            Error_Handler();
        }

        int argsize = validateHostCommand(cmd);
        if (argsize < 0) {
            continue;
        } else if (argsize > 0) {
            memset(txbuff, 0, argsize);
            rc = HAL_SPI_TransmitReceive(context.hspi, txbuff, rxbuff, argsize,
                    100);
            //rc = HAL_SPI_Receive(context.hspi, rxbuff, argsize, 100);
            //rc = HAL_SPI_Receive(context.hspi, rxbuff, argsize, HAL_MAX_DELAY);
            if (rc == HAL_TIMEOUT) {
                continue;
            } else if (rc != HAL_OK) {
                Error_Handler();
            }
        }
        lastcmd = cmd;
        cmdcount++;
        int respsize = processHostCommand(&hcomm, cmd, rxbuff, argsize,
                txbuff + 4, sizeof(txbuff) - 4);
        if (respsize > 0) {
            txbuff[2] = TRACKCAM_MAGIC_RESP;
            txbuff[3] = cmd;
            int rc = HAL_SPI_TransmitReceive(context.hspi, txbuff + 2, rxbuff,
                    respsize + 2, 100);
            if (rc == HAL_TIMEOUT) {
                continue;
            } else if (rc != HAL_OK) {
                Error_Handler();
            }
        }
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim == context.htimLed) {
        scheduleLED(&hled);
        maintainServoLF(&hservo);
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* handle)
{
    if (handle == context.hadc) {
        scheduleServo(&hservo);
    }
}

void trackCamErrorHandler()
{
    // set servo mode to idle
    int i;
    for (i = 0; i < SERVO_NUM; i++) {
        SERVO_CONFIG(&hservo, i).mode = SERVO_IDLE;
    }
    commitServoConfig(&hservo);

    // set led mode to emergency
    LED_CONFIG(&hled, LED_SEQ_LEVEL_USER).type[LED_BLUE] = LED_SEQ_TYPE_EMERGENCY;
    LED_CONFIG(&hled, LED_SEQ_LEVEL_USER).type[LED_RED] = LED_SEQ_TYPE_EMERGENCY;
    commitLEDConfig(&hled, LED_SEQ_LEVEL_USER);

    while(1);
}

