/* USER CODE BEGIN Header */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "fdcan.h"
#include "i2c.h"
#include "tim.h"
#include "usb.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ws2812.h"
#include "motor.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "ssd1306_tests.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Watch these in CubeIDE Live Expressions while testing motor A. */

volatile uint32_t      dbg_motorA_current_mA = 0;
volatile Motor_State_t dbg_motorA_state      = MOTOR_STATE_DISABLED;
volatile Motor_Fault_t dbg_motorA_fault      = MOTOR_FAULT_NONE;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_FDCAN2_Init();
  MX_TIM16_Init();
  MX_TIM17_Init();
  MX_USB_PCD_Init();
  MX_TIM8_Init();
  /* USER CODE BEGIN 2 */
  WS2812_Init();
  /* Boot beacon: LED 0 dim blue the moment firmware reaches main().
   * If this never lights, the code isn't running (see BOOT0 note) or the
   * WS2812 wiring/timer is the problem, not the later init steps. */
  WS2812_Init();
  WS2812_SetAll(0, 0, 0);
  WS2812_SetPixel(0, 0, 0, 40);
  WS2812_Show();

  /* Bench test: motor A only, forward, full duty, runs continuously. */
  Motor_Init();
  Motor_Select(MOTOR_1);
  Motor_SetDirection(MOTOR_1, MOTOR_DIR_FORWARD);
  Motor_SetSpeed(MOTOR_1, 100);
  Motor_Start(MOTOR_1);

  /* OLED (SSD1306 on I2C1: PB7 SDA / PB8 SCL). Probe first; if nothing
   * ACKs at SSD1306_I2C_ADDR (0x3C; try 0x3D in ssd1306_config.h if your
   * module is strapped that way) skip it instead of blocking the motor loop.
   * LED 1 = green: OLED found, red: no ACK on the bus. */
  if (HAL_I2C_IsDeviceReady(&hi2c1, SSD1306_I2C_ADDR, 2, 20) == HAL_OK)
  {
    ssd1306_Init();
    ssd1306_Fill(Black);
    ssd1306_DrawRectangle(0, 0, SSD1306_WIDTH - 1, SSD1306_HEIGHT - 1, White);
    ssd1306_SetCursor(8, 8);
    ssd1306_WriteString("Wiper Board", Font_11x18, White);
    ssd1306_SetCursor(8, 32);
    ssd1306_WriteString("OLED test OK", Font_7x10, White);
    ssd1306_UpdateScreen();
    WS2812_SetPixel(1, 0, 40, 0);
  }
  else
  {
    WS2812_SetPixel(1, 40, 0, 0);
  }
  WS2812_Show();

  uint32_t led_tick = HAL_GetTick();
  uint32_t led_index = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  while (1)
  {
    /* USER CODE BEGIN WHILE */
    /* Nothing in this loop may block: Motor_Process() needs to run every ms
     * to catch STSPIN948 faults and stalls. */
    Motor_Process();

    dbg_motorA_current_mA = Motor_GetCurrent_mA(MOTOR_1);
    dbg_motorA_state      = Motor_GetState(MOTOR_1);
    dbg_motorA_fault      = Motor_GetFault(MOTOR_1);

    if ((HAL_GetTick() - led_tick) >= 200U)
    {
      led_tick = HAL_GetTick();

      switch (dbg_motorA_state)
      {
        case MOTOR_STATE_RUNNING:                  /* green chase: running fine */
          WS2812_SetAll(0, 0, 0);
          WS2812_SetPixel(led_index, 0, 255, 0);
          led_index = (led_index + 1U) % WS2812_NUM_LEDS;
          break;

        case MOTOR_STATE_FAULT_LOCKOUT:            /* solid red: gave up */
          WS2812_SetAll(255, 0, 0);
          break;

        default:                                   /* solid yellow: starting / retrying */
          WS2812_SetAll(255, 160, 0);
          break;
      }
      WS2812_Show();
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Make the power stage safe before hanging: both bridges off, STSPIN948
   * in standby. Harmless if GPIO clocks aren't on yet. */
  __disable_irq();
  HAL_GPIO_WritePin(ENA_GPIO_Port, ENA_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ENB_GPIO_Port, ENB_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(NSTBY_GPIO_Port, NSTBY_Pin, GPIO_PIN_RESET);
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
