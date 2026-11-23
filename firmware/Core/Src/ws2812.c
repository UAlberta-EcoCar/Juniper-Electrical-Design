/**
  ******************************************************************************
  * @file    ws2812.c
  * @brief   Driver for a WS2812B addressable RGB LED chain driven by TIM8_CH1
  *          (RGB_LED_PWM / PA15).
  ******************************************************************************
  * @note    4 LEDs in series; the first LED's DIN is wired directly to PA15,
  *          and each LED re-times and forwards the remaining bits to the
  *          next one down the chain. Data is sent MSB-first, one LED's
  *          worth (24 bits, GRB order) at a time, starting with the LED
  *          closest to the MCU.
  *
  *          TIM8 is clocked from APB2 at 16 MHz with Prescaler = 0 (see
  *          tim.c / Wiper-Board-Firmwire.ioc, RCC.APB2TimFreq_Value=16000000),
  *          so each timer tick is 62.5 ns. MX_TIM8_Init() leaves ARR at the
  *          CubeMX default (65535); WS2812_Init() below re-targets it to the
  *          WS2812B 800 kHz bit rate.
  *
  *            Bit period : 20 ticks x 62.5 ns = 1.25 us
  *            '1' bit    : 13 ticks high      = 812.5 ns  (spec: 0.8us +/-150ns)
  *            '0' bit    : 6  ticks high      = 375 ns    (spec: 0.4us +/-150ns)
  *            Reset/latch: >=50 us low after the last LED's data
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "ws2812.h"
#include "tim.h"

/* Private define --------------------------------------------------------------*/
#define WS2812_TIM_CLK_HZ     16000000UL
#define WS2812_PWM_PERIOD     20U   /* ARR = WS2812_PWM_PERIOD - 1 */
#define WS2812_T1H_TICKS      13U
#define WS2812_T0H_TICKS      6U
#define WS2812_RESET_US       60U

/* Private variables -----------------------------------------------------------*/
/* Local frame buffer; index 0 is the LED wired directly to PA15. */
static WS2812_Color_t ws2812_pixels[WS2812_NUM_LEDS];

/* Private function prototypes --------------------------------------------------*/
static void WS2812_SendByte(uint8_t data);
static void WS2812_Reset(void);

/* Exported functions -----------------------------------------------------------*/

void WS2812_Init(void)
{
  __HAL_TIM_SET_PRESCALER(&htim8, 0);
  __HAL_TIM_SET_AUTORELOAD(&htim8, WS2812_PWM_PERIOD - 1U);
  __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, 0);
  __HAL_TIM_SET_COUNTER(&htim8, 0);
}

void WS2812_SetPixel(uint32_t index, uint8_t red, uint8_t green, uint8_t blue)
{
  if (index < WS2812_NUM_LEDS)
  {
    ws2812_pixels[index].red = red;
    ws2812_pixels[index].green = green;
    ws2812_pixels[index].blue = blue;
  }
}

void WS2812_SetAll(uint8_t red, uint8_t green, uint8_t blue)
{
  for (uint32_t i = 0; i < WS2812_NUM_LEDS; i++)
  {
    WS2812_SetPixel(i, red, green, blue);
  }
}

void WS2812_Show(void)
{
  __disable_irq();

  HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1);

  for (uint32_t i = 0; i < WS2812_NUM_LEDS; i++)
  {
    /* WS2812B data order is GRB, MSB first, LED 0 (nearest PA15) sent first */
    WS2812_SendByte(ws2812_pixels[i].green);
    WS2812_SendByte(ws2812_pixels[i].red);
    WS2812_SendByte(ws2812_pixels[i].blue);
  }

  WS2812_Reset();
  HAL_TIM_PWM_Stop(&htim8, TIM_CHANNEL_1);

  __enable_irq();
}

/* Private functions -------------------------------------------------------------*/

/**
  * @brief  Shifts one byte out on TIM8_CH1 as WS2812B bits, MSB first.
  * @note   Assumes PWM generation on TIM_CHANNEL_1 is already running.
  *         Each iteration loads the compare value for the next bit and then
  *         blocks until the timer's update event marks the end of the
  *         current 1.25us period, so one loop pass == one transmitted bit.
  * @param  data Byte to transmit.
  * @retval None
  */
static void WS2812_SendByte(uint8_t data)
{
  for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1U)
  {
    uint32_t ccr = (data & mask) ? WS2812_T1H_TICKS : WS2812_T0H_TICKS;

    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, ccr);

    /* Bounded wait: if TIM8 ever isn't running, give up instead of
     * hanging the whole firmware with interrupts disabled. */
    for (uint32_t guard = 100000U;
         (guard != 0U) && (__HAL_TIM_GET_FLAG(&htim8, TIM_FLAG_UPDATE) == RESET);
         guard--)
    {
    }
    __HAL_TIM_CLEAR_FLAG(&htim8, TIM_FLAG_UPDATE);
  }
}

/**
  * @brief  Holds TIM8_CH1 low for the WS2812B reset/latch time by keeping
  *         PWM running with a 0 duty cycle for enough periods.
  * @retval None
  */
static void WS2812_Reset(void)
{
  uint32_t periods = (WS2812_RESET_US * (WS2812_TIM_CLK_HZ / 1000000UL)) / WS2812_PWM_PERIOD;

  __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, 0);

  for (uint32_t i = 0; i < periods; i++)
  {
    for (uint32_t guard = 100000U;
         (guard != 0U) && (__HAL_TIM_GET_FLAG(&htim8, TIM_FLAG_UPDATE) == RESET);
         guard--)
    {
    }
    __HAL_TIM_CLEAR_FLAG(&htim8, TIM_FLAG_UPDATE);
  }
}
