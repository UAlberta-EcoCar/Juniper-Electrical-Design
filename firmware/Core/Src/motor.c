/**
  ******************************************************************************
  * @file    motor.c
  * @brief   Driver for the two STSPIN948 full-bridge motor channels.
  *          See motor.h for the board/datasheet assumptions.
  ******************************************************************************
  */
#include "motor.h"
#include "tim.h"
#include "adc.h"

/* Tunables ------------------------------------------------------------------*/
#define MOTOR_PWM_FREQ_HZ          20000UL  /* inaudible, far below the 500 kHz max   */
#define MOTOR_DEFAULT_DUTY_PCT     100U

#define MOTOR_REVERSE_DWELL_MS     150U     /* coast time before flipping direction   */
#define MOTOR_ENABLE_TIMEOUT_MS    5U       /* EN RC (39k * 10n) needs ~0.6 ms        */
#define MOTOR_FAULT_RETRY_MS       100U     /* EN held low before re-trying           */
#define MOTOR_FAULT_MAX_RETRIES    3U       /* then lock out                          */
#define MOTOR_FAULT_RETRY_RESET_MS 1000U    /* clean run time that resets retries     */

#define MOTOR_STALL_CURRENT_MA     900U     /* just under the ~1.0 A hardware limit   */
#define MOTOR_STALL_TIME_MS        500U     /* longer than normal start-up inrush     */

/* STSPIN948 datasheet (DS14271 Rev 4) ---------------------------------------*/
#define STSPIN_MIN_PULSE_NS        280UL    /* tpulse, Table 2                        */
#define STSPIN_WAKE_BOOT_US        180UL    /* tWAKE 10 us + tBOOT 170 us             */

/* Board ---------------------------------------------------------------------*/
#define BOARD_VDDA_MV              3300UL
#define BOARD_ADC_FULL_SCALE       4095UL
#define BOARD_SENSE_MV_PER_A       500UL    /* 50 mOhm * ACL 10, OFFSETx low         */

/* Private types -------------------------------------------------------------*/
typedef struct
{
  TIM_HandleTypeDef *htim;
  uint32_t           channel;
  GPIO_TypeDef      *ph_port;
  uint16_t           ph_pin;
  GPIO_TypeDef      *en_port;
  uint16_t           en_pin;
  uint32_t           adc_channel;
} Motor_HwConfig_t;

typedef struct
{
  Motor_State_t state;
  Motor_State_t target;          /* RUNNING or BRAKING, used by ENABLING     */
  Motor_Fault_t fault;
  Motor_Dir_t   direction;       /* what PHx is set to                       */
  Motor_Dir_t   pending_dir;     /* applied at the end of REVERSING          */
  uint8_t       duty_percent;
  uint8_t       retries;
  uint8_t       low_samples;     /* consecutive EN-low reads while enabled   */
  uint32_t      state_tick;      /* HAL tick when the state was entered      */
  uint32_t      stall_tick;      /* HAL tick when current first hit stall    */
  uint32_t      current_mA;      /* filtered, on-time scaled                 */
} Motor_Ctx_t;

/* Private variables ---------------------------------------------------------*/
static const Motor_HwConfig_t motor_hw[MOTOR_COUNT] =
{
  [MOTOR_1] = { &htim16, TIM_CHANNEL_1, PHA_GPIO_Port, PHA_Pin, ENA_GPIO_Port, ENA_Pin, ADC_CHANNEL_4  },
  [MOTOR_2] = { &htim17, TIM_CHANNEL_1, PHB_GPIO_Port, PHB_Pin, ENB_GPIO_Port, ENB_Pin, ADC_CHANNEL_15 },
};

static Motor_Ctx_t motor[MOTOR_COUNT];
static Motor_Id_t  motor_selected = MOTOR_1;

static uint32_t pwm_period_ticks;   /* ARR + 1                              */
static uint32_t pwm_min_ticks;      /* shortest legal on/off pulse in ticks */
static uint32_t last_process_tick;

/* Private helpers -----------------------------------------------------------*/

/* TIM16/TIM17 sit on APB2: timer clock = PCLK2, or 2 x PCLK2 if APB2 is divided. */
static uint32_t motor_timer_clock_hz(void)
{
  RCC_ClkInitTypeDef clk;
  uint32_t flash_latency;

  HAL_RCC_GetClockConfig(&clk, &flash_latency);
  uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
  return (clk.APB2CLKDivider == RCC_HCLK_DIV1) ? pclk2 : (2U * pclk2);
}

static void motor_delay_us(uint32_t us)
{
  /* Coarse busy-wait, only used once at start-up. ~4 cycles per iteration,
   * so this always waits at least as long as asked. */
  volatile uint32_t n = (SystemCoreClock / 4000000UL + 1U) * us;
  while (n--) { __NOP(); }
}

static void motor_en_low(Motor_Id_t m)
{
  HAL_GPIO_WritePin(motor_hw[m].en_port, motor_hw[m].en_pin, GPIO_PIN_RESET);
}

static void motor_en_release(Motor_Id_t m)
{
  /* Open-drain: SET = let the 39k pull-up (and the STSPIN948) decide. */
  HAL_GPIO_WritePin(motor_hw[m].en_port, motor_hw[m].en_pin, GPIO_PIN_SET);
}

static uint8_t motor_en_is_high(Motor_Id_t m)
{
  return (HAL_GPIO_ReadPin(motor_hw[m].en_port, motor_hw[m].en_pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint32_t motor_duty_to_ccr(uint8_t duty_percent)
{
  uint32_t ccr = ((uint32_t)duty_percent * pwm_period_ticks) / 100U;

  if (ccr == 0U)
  {
    return 0U;
  }
  if (ccr < pwm_min_ticks)
  {
    ccr = pwm_min_ticks;              /* on-pulse too short for the driver  */
  }
  if ((pwm_period_ticks - ccr) < pwm_min_ticks)
  {
    ccr = pwm_period_ticks;           /* off-pulse too short: go fully on    */
  }
  return ccr;
}

static void motor_set_pwm(Motor_Id_t m, uint8_t duty_percent)
{
  __HAL_TIM_SET_COMPARE(motor_hw[m].htim, motor_hw[m].channel, motor_duty_to_ccr(duty_percent));
}

static void motor_write_ph(Motor_Id_t m, Motor_Dir_t dir)
{
  HAL_GPIO_WritePin(motor_hw[m].ph_port, motor_hw[m].ph_pin,
                    (dir == MOTOR_DIR_REVERSE) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  motor[m].direction = dir;
}

static void motor_enter(Motor_Id_t m, Motor_State_t s)
{
  Motor_Ctx_t *c = &motor[m];

  c->state       = s;
  c->state_tick  = HAL_GetTick();
  c->low_samples = 0U;
  c->stall_tick  = 0U;

  switch (s)
  {
    case MOTOR_STATE_ENABLING:
      motor_set_pwm(m, (c->target == MOTOR_STATE_RUNNING) ? c->duty_percent : 0U);
      motor_en_release(m);
      break;

    case MOTOR_STATE_RUNNING:
      motor_set_pwm(m, c->duty_percent);
      break;

    case MOTOR_STATE_BRAKING:
      motor_set_pwm(m, 0U);
      break;

    case MOTOR_STATE_DISABLED:
    case MOTOR_STATE_REVERSING:
    case MOTOR_STATE_FAULT_RETRY:
    case MOTOR_STATE_FAULT_LOCKOUT:
    default:
      /* EN low turns the bridge off and, held below 0.4 V, also clears a
       * latched overcurrent fault (datasheet 5.7). */
      motor_set_pwm(m, 0U);
      motor_en_low(m);
      break;
  }
}

static void motor_fault(Motor_Id_t m, Motor_Fault_t f)
{
  Motor_Ctx_t *c = &motor[m];

  c->fault = f;

  if ((f == MOTOR_FAULT_STALL) || (c->retries >= MOTOR_FAULT_MAX_RETRIES))
  {
    motor_enter(m, MOTOR_STATE_FAULT_LOCKOUT);
  }
  else
  {
    c->retries++;
    motor_enter(m, MOTOR_STATE_FAULT_RETRY);
  }
}

static uint32_t motor_read_sense_mV(Motor_Id_t m)
{
  ADC_ChannelConfTypeDef cfg = {0};
  uint32_t raw = 0U;

  cfg.Channel      = motor_hw[m].adc_channel;
  cfg.Rank         = ADC_REGULAR_RANK_1;
  cfg.SamplingTime = ADC_SAMPLETIME_24CYCLES_5;   /* VA/VB go through 100R + 100p */
  cfg.SingleDiff   = ADC_SINGLE_ENDED;
  cfg.OffsetNumber = ADC_OFFSET_NONE;
  cfg.Offset       = 0;

  if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK)
  {
    return 0U;
  }
  if (HAL_ADC_Start(&hadc1) == HAL_OK)
  {
    if (HAL_ADC_PollForConversion(&hadc1, 1U) == HAL_OK)
    {
      raw = HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
  }
  return (raw * BOARD_VDDA_MV) / BOARD_ADC_FULL_SCALE;
}

static void motor_update_current(Motor_Id_t m)
{
  Motor_Ctx_t *c = &motor[m];
  uint32_t i_mA = (motor_read_sense_mV(m) * 1000U) / BOARD_SENSE_MV_PER_A;

  /* The shunt only carries current while the bridge drives (slow decay
   * recirculates through the low sides), so an unsynchronised sample reads
   * roughly I * duty. Scale back up to estimate the on-time current. */
  if ((c->state == MOTOR_STATE_RUNNING) && (c->duty_percent >= 20U) && (c->duty_percent < 100U))
  {
    i_mA = (i_mA * 100U) / c->duty_percent;
  }

  /* First-order filter, alpha = 1/8 (time constant ~8 ms at a 1 ms rate). */
  int32_t err = (int32_t)i_mA - (int32_t)c->current_mA;
  c->current_mA = (uint32_t)((int32_t)c->current_mA + err / 8);
}

static void motor_process_one(Motor_Id_t m, uint32_t now)
{
  Motor_Ctx_t *c = &motor[m];
  uint32_t in_state = now - c->state_tick;

  motor_update_current(m);

  switch (c->state)
  {
    case MOTOR_STATE_ENABLING:
      if (motor_en_is_high(m))
      {
        motor_enter(m, c->target);
      }
      else if (in_state >= MOTOR_ENABLE_TIMEOUT_MS)
      {
        motor_fault(m, MOTOR_FAULT_ENABLE_TIMEOUT);
      }
      break;

    case MOTOR_STATE_RUNNING:
    case MOTOR_STATE_BRAKING:
      /* Released EN reading low = STSPIN948 is pulling nFAULT. Two samples
       * in a row so a single noisy read doesn't stop the wiper. */
      if (!motor_en_is_high(m))
      {
        if (++c->low_samples >= 2U)
        {
          motor_fault(m, MOTOR_FAULT_DRIVER);
          break;
        }
      }
      else
      {
        c->low_samples = 0U;
      }

      if ((c->state == MOTOR_STATE_RUNNING) && (in_state >= MOTOR_FAULT_RETRY_RESET_MS))
      {
        c->retries = 0U;
      }

      if ((c->state == MOTOR_STATE_RUNNING) && (c->current_mA >= MOTOR_STALL_CURRENT_MA))
      {
        if (c->stall_tick == 0U)
        {
          c->stall_tick = now | 1U;   /* never 0 while armed */
        }
        else if ((now - c->stall_tick) >= MOTOR_STALL_TIME_MS)
        {
          motor_fault(m, MOTOR_FAULT_STALL);
        }
      }
      else
      {
        c->stall_tick = 0U;
      }
      break;

    case MOTOR_STATE_REVERSING:
      if (in_state >= MOTOR_REVERSE_DWELL_MS)
      {
        motor_write_ph(m, c->pending_dir);
        c->target = MOTOR_STATE_RUNNING;
        motor_enter(m, MOTOR_STATE_ENABLING);
      }
      break;

    case MOTOR_STATE_FAULT_RETRY:
      if (in_state >= MOTOR_FAULT_RETRY_MS)
      {
        motor_enter(m, MOTOR_STATE_ENABLING);   /* keeps the previous target */
      }
      break;

    case MOTOR_STATE_DISABLED:
    case MOTOR_STATE_FAULT_LOCKOUT:
    default:
      break;
  }
}

/* Exported functions --------------------------------------------------------*/

void Motor_Init(void)
{
  /* Hold everything off while the timers are set up. */
  HAL_GPIO_WritePin(NSTBY_GPIO_Port, NSTBY_Pin, GPIO_PIN_RESET);

  uint32_t tim_clk = motor_timer_clock_hz();
  pwm_period_ticks = tim_clk / MOTOR_PWM_FREQ_HZ;          /* 800 @ 16 MHz, 8500 @ 170 MHz */
  pwm_min_ticks    = ((tim_clk / 1000UL) * STSPIN_MIN_PULSE_NS + 999999UL) / 1000000UL;

  for (uint32_t i = 0; i < MOTOR_COUNT; i++)
  {
    Motor_Id_t m = (Motor_Id_t)i;
    const Motor_HwConfig_t *hw = &motor_hw[m];

    motor_en_low(m);
    motor_write_ph(m, MOTOR_DIR_FORWARD);

    __HAL_TIM_SET_PRESCALER(hw->htim, 0U);
    __HAL_TIM_SET_AUTORELOAD(hw->htim, pwm_period_ticks - 1U);
    __HAL_TIM_SET_COMPARE(hw->htim, hw->channel, 0U);
    HAL_TIM_GenerateEvent(hw->htim, TIM_EVENTSOURCE_UPDATE);   /* load PSC/ARR/CCR now */
    HAL_TIM_PWM_Start(hw->htim, hw->channel);

    motor[m].state        = MOTOR_STATE_DISABLED;
    motor[m].target       = MOTOR_STATE_RUNNING;
    motor[m].fault        = MOTOR_FAULT_NONE;
    motor[m].pending_dir  = MOTOR_DIR_FORWARD;
    motor[m].duty_percent = MOTOR_DEFAULT_DUTY_PCT;
    motor[m].retries      = 0U;
    motor[m].current_mA   = 0U;
    motor[m].state_tick   = HAL_GetTick();
  }

  (void)HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

  /* Leave standby. The charge pump needs tWAKE + tBOOT before the high
   * sides can switch; EN stays low until Motor_Start(). */
  HAL_GPIO_WritePin(NSTBY_GPIO_Port, NSTBY_Pin, GPIO_PIN_SET);
  motor_delay_us(STSPIN_WAKE_BOOT_US);

  motor_selected    = MOTOR_1;
  last_process_tick = HAL_GetTick();
}

void Motor_Process(void)
{
  uint32_t now = HAL_GetTick();

  if (now == last_process_tick)
  {
    return;                       /* run at most once per 1 ms tick */
  }
  last_process_tick = now;

  for (uint32_t i = 0; i < MOTOR_COUNT; i++)
  {
    motor_process_one((Motor_Id_t)i, now);
  }
}

void Motor_Select(Motor_Id_t m)
{
  if (m >= MOTOR_COUNT)
  {
    return;
  }
  if (m != motor_selected)
  {
    Motor_Stop(motor_selected);
    motor_selected = m;
  }
}

Motor_Id_t Motor_GetSelected(void)
{
  return motor_selected;
}

void Motor_SetDirection(Motor_Id_t m, Motor_Dir_t dir)
{
  if (m >= MOTOR_COUNT)
  {
    return;
  }

  Motor_Ctx_t *c = &motor[m];

  switch (c->state)
  {
    case MOTOR_STATE_RUNNING:
    case MOTOR_STATE_ENABLING:
      if (dir != c->direction)
      {
        /* Never flip PHx under a spinning motor: coast, wait, restart. */
        c->pending_dir = dir;
        motor_enter(m, MOTOR_STATE_REVERSING);
      }
      break;

    case MOTOR_STATE_REVERSING:
      c->pending_dir = dir;
      break;

    default:
      /* Stopped, braking (PWM = 0, PHx ignored) or faulted: apply directly. */
      c->pending_dir = dir;
      motor_write_ph(m, dir);
      break;
  }
}

Motor_Dir_t Motor_GetDirection(Motor_Id_t m)
{
  if (m >= MOTOR_COUNT)
  {
    return MOTOR_DIR_FORWARD;
  }
  /* Report where the motor is headed, including a pending reversal. */
  return (motor[m].state == MOTOR_STATE_REVERSING) ? motor[m].pending_dir : motor[m].direction;
}

void Motor_SetSpeed(Motor_Id_t m, uint8_t duty_percent)
{
  if (m >= MOTOR_COUNT)
  {
    return;
  }
  if (duty_percent > 100U)
  {
    duty_percent = 100U;
  }

  motor[m].duty_percent = duty_percent;

  if ((motor[m].state == MOTOR_STATE_RUNNING) ||
      ((motor[m].state == MOTOR_STATE_ENABLING) && (motor[m].target == MOTOR_STATE_RUNNING)))
  {
    motor_set_pwm(m, duty_percent);
  }
}

uint8_t Motor_GetSpeed(Motor_Id_t m)
{
  return (m < MOTOR_COUNT) ? motor[m].duty_percent : 0U;
}

void Motor_Start(Motor_Id_t m)
{
  if (m >= MOTOR_COUNT)
  {
    return;
  }

  Motor_Ctx_t *c = &motor[m];

  switch (c->state)
  {
    case MOTOR_STATE_DISABLED:
      c->target = MOTOR_STATE_RUNNING;
      motor_enter(m, MOTOR_STATE_ENABLING);
      break;

    case MOTOR_STATE_BRAKING:
      motor_enter(m, MOTOR_STATE_RUNNING);
      break;

    case MOTOR_STATE_ENABLING:
    case MOTOR_STATE_FAULT_RETRY:
      c->target = MOTOR_STATE_RUNNING;
      break;

    default:
      break;    /* already running / reversing, or locked out */
  }
}

void Motor_Stop(Motor_Id_t m)
{
  if ((m >= MOTOR_COUNT) || (motor[m].state == MOTOR_STATE_FAULT_LOCKOUT))
  {
    return;
  }
  if (motor[m].state == MOTOR_STATE_REVERSING)
  {
    motor_write_ph(m, motor[m].pending_dir);   /* keep the requested direction */
  }
  motor_enter(m, MOTOR_STATE_DISABLED);
}

void Motor_Brake(Motor_Id_t m)
{
  if (m >= MOTOR_COUNT)
  {
    return;
  }

  Motor_Ctx_t *c = &motor[m];

  switch (c->state)
  {
    case MOTOR_STATE_RUNNING:
      motor_enter(m, MOTOR_STATE_BRAKING);
      break;

    case MOTOR_STATE_DISABLED:
    case MOTOR_STATE_REVERSING:
      if (c->state == MOTOR_STATE_REVERSING)
      {
        motor_write_ph(m, c->pending_dir);
      }
      c->target = MOTOR_STATE_BRAKING;
      motor_enter(m, MOTOR_STATE_ENABLING);
      break;

    case MOTOR_STATE_ENABLING:
    case MOTOR_STATE_FAULT_RETRY:
      c->target = MOTOR_STATE_BRAKING;
      if (c->state == MOTOR_STATE_ENABLING)
      {
        motor_set_pwm(m, 0U);
      }
      break;

    default:
      break;
  }
}

uint8_t Motor_IsRunning(Motor_Id_t m)
{
  if (m >= MOTOR_COUNT)
  {
    return 0U;
  }

  switch (motor[m].state)
  {
    case MOTOR_STATE_RUNNING:
    case MOTOR_STATE_REVERSING:
      return 1U;
    case MOTOR_STATE_ENABLING:
    case MOTOR_STATE_FAULT_RETRY:
      return (motor[m].target == MOTOR_STATE_RUNNING) ? 1U : 0U;
    default:
      return 0U;
  }
}

Motor_State_t Motor_GetState(Motor_Id_t m)
{
  return (m < MOTOR_COUNT) ? motor[m].state : MOTOR_STATE_DISABLED;
}

Motor_Fault_t Motor_GetFault(Motor_Id_t m)
{
  return (m < MOTOR_COUNT) ? motor[m].fault : MOTOR_FAULT_NONE;
}

void Motor_ClearFault(Motor_Id_t m)
{
  if (m >= MOTOR_COUNT)
  {
    return;
  }
  motor[m].fault   = MOTOR_FAULT_NONE;
  motor[m].retries = 0U;
  if (motor[m].state == MOTOR_STATE_FAULT_LOCKOUT)
  {
    motor_enter(m, MOTOR_STATE_DISABLED);
  }
}

uint32_t Motor_GetCurrent_mA(Motor_Id_t m)
{
  return (m < MOTOR_COUNT) ? motor[m].current_mA : 0U;
}
