#ifndef MPU6050_H
#define MPU6050_H

#include "stm32l4xx_hal.h" // Replace with your family header if different
#include <math.h>

/* ---- I2C address (7-bit, shifted left 1 for HAL) ---------------------- */
#define MPU6050_ADDR         (0x68 << 1)

/* ---- Register addresses ------------------------------------------------ */
#define REG_SMPLRT_DIV       0x19
#define REG_CONFIG           0x1A
#define REG_GYRO_CONFIG      0x1B
#define REG_ACCEL_CONFIG     0x1C
#define REG_INT_ENABLE       0x38
#define REG_ACCEL_XOUT_H     0x3B
#define REG_TEMP_OUT_H       0x41
#define REG_GYRO_XOUT_H      0x43
#define REG_PWR_MGMT_1       0x6B
#define REG_WHO_AM_I         0x75

/* Official documented WHO_AM_I value per the InvenSense MPU-6000/MPU-6050
 * Register Map and Descriptions (Rev 4.0, section 4.34). Some counterfeit/
 * clone chips report a different value (0x72 is a commonly-seen example).
 * This driver never silently assumes an alternate value is fine — YOU
 * check what your chip reports with MPU6050_Check_WhoAmI() and explicitly
 * pass that value into MPU6050_Init(). See usage note below.            */
#define MPU6050_WHOAMI_DEFAULT   0x68

/* ---- Status codes -------------------------------------------------------*/
#define MPU6050_OK      0
#define MPU6050_ERROR   1

/* ---- Data struct ---------------------------------------------------------
 * Ax/Ay/Az: g          Gx/Gy/Gz: deg/s          Temperature: degrees C
 * pitch/roll: filtered absolute attitude, in degrees (gravity-referenced,
 * so these do NOT drift over time — unlike yaw, which has no absolute
 * reference without a magnetometer and must be integrated from Gz alone).
 * --------------------------------------------------------------------------*/
typedef struct {
    float Ax, Ay, Az;
    float Gx, Gy, Gz;
    float Temperature;

    float pitch;   /* filtered, gravity-referenced, degrees */
    float roll;    /* filtered, gravity-referenced, degrees */
} MPU6050_t;

/**
 * @brief  Reads the raw WHO_AM_I register and hands the value back to you.
 *         Purely diagnostic — no comparison, no pass/fail judgment.
 * @retval MPU6050_OK if the I2C transaction itself succeeded (regardless
 *         of the value read), MPU6050_ERROR if nothing responded at all.
 */
uint8_t MPU6050_Check_WhoAmI(I2C_HandleTypeDef *hi2c, uint8_t *outValue);

/**
 * @brief  Verifies WHO_AM_I matches expectedWhoAmI, wakes the sensor,
 *         and configures sample rate + full-scale ranges.
 * @param  expectedWhoAmI  The value YOU expect for your specific hardware.
 *                         Use MPU6050_WHOAMI_DEFAULT (0x68) for a genuine
 *                         chip. If your hardware reports something else
 *                         (check with MPU6050_Check_WhoAmI first), pass
 *                         that value explicitly — this driver will not
 *                         guess or silently accept it for you.
 * @retval MPU6050_OK on success, MPU6050_ERROR on any I2C failure or a
 *         WHO_AM_I mismatch against expectedWhoAmI.
 */
uint8_t MPU6050_Init(I2C_HandleTypeDef *hi2c, uint8_t expectedWhoAmI);

/**
 * @brief  Calibrates the gyroscope zero-rate bias by averaging 'samples'
 *         readings while the sensor is completely stationary.
 *         MUST be called only after MPU6050_Init() has succeeded.
 * @retval MPU6050_OK if all samples were read successfully, MPU6050_ERROR
 *         if any I2C read failed partway through (bias values are left
 *         at whatever partial average was computed so far — re-run if
 *         this happens).
 */
uint8_t MPU6050_Calibrate(I2C_HandleTypeDef *hi2c, uint16_t samples);

/**
 * @brief  Reads accel + gyro + temperature, applies calibration bias to
 *         the gyro, and updates the complementary-filtered pitch/roll.
 * @param  dt  Loop time step in seconds (e.g. 0.010f for a 10ms loop).
 * @retval MPU6050_OK on success, MPU6050_ERROR if the I2C read failed
 *         (in which case DataStruct is left unmodified from its previous
 *         values — check the return code rather than assuming success).
 */
uint8_t MPU6050_Read_All(I2C_HandleTypeDef *hi2c, MPU6050_t *mpu, float dt);

#endif /* MPU6050_H */
