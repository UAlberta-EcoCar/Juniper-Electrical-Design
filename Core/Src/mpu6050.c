#include "mpu6050.h"

#define MPU6050_I2C_TIMEOUT   100

/* Gyroscope zero-rate bias, measured once via MPU6050_Calibrate().
 * Defaults to 0 until calibration actually runs successfully — if you
 * see continuous rotation with the sensor sitting still, the most
 * likely cause is that Init failed (e.g. wrong expectedWhoAmI) and
 * Calibrate was therefore never called, leaving these at 0.          */
static float gyro_bias_x = 0.0f;
static float gyro_bias_y = 0.0f;
static float gyro_bias_z = 0.0f;

uint8_t MPU6050_Check_WhoAmI(I2C_HandleTypeDef *hi2c, uint8_t *outValue)
{
    if (HAL_I2C_Mem_Read(hi2c, MPU6050_ADDR, REG_WHO_AM_I, 1,
                          outValue, 1, MPU6050_I2C_TIMEOUT) != HAL_OK) {
        return MPU6050_ERROR;
    }
    return MPU6050_OK;
}

uint8_t MPU6050_Init(I2C_HandleTypeDef *hi2c, uint8_t expectedWhoAmI)
{
    uint8_t check = 0;
    uint8_t data;

    if (MPU6050_Check_WhoAmI(hi2c, &check) != MPU6050_OK) {
        return MPU6050_ERROR;   /* I2C transaction itself failed */
    }

    /* Compare against exactly what the caller told us to expect — no
     * silent acceptance of any alternate/clone value baked in here.   */
    if (check != expectedWhoAmI) {
        return MPU6050_ERROR;
    }

    /* Wake the device up (clear SLEEP bit in PWR_MGMT_1) */
    data = 0x00;
    if (HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, REG_PWR_MGMT_1, 1,
                           &data, 1, MPU6050_I2C_TIMEOUT) != HAL_OK) {
        return MPU6050_ERROR;
    }

    /* Sample rate divider: 1kHz gyro output / (1+7) = 125 Hz sample rate */
    data = 0x07;
    if (HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, REG_SMPLRT_DIV, 1,
                           &data, 1, MPU6050_I2C_TIMEOUT) != HAL_OK) {
        return MPU6050_ERROR;
    }

    /* Accelerometer full-scale: +/-2g -> 16384 LSB/g */
    data = 0x00;
    if (HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, REG_ACCEL_CONFIG, 1,
                           &data, 1, MPU6050_I2C_TIMEOUT) != HAL_OK) {
        return MPU6050_ERROR;
    }

    /* Gyroscope full-scale: +/-250 deg/s -> 131 LSB/(deg/s) */
    data = 0x00;
    if (HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, REG_GYRO_CONFIG, 1,
                           &data, 1, MPU6050_I2C_TIMEOUT) != HAL_OK) {
        return MPU6050_ERROR;
    }

    return MPU6050_OK;
}

uint8_t MPU6050_Calibrate(I2C_HandleTypeDef *hi2c, uint16_t samples)
{
    uint8_t rec_data[6];
    int16_t raw_gx, raw_gy, raw_gz;
    double sum_x = 0, sum_y = 0, sum_z = 0;

    for (uint16_t i = 0; i < samples; i++) {
        if (HAL_I2C_Mem_Read(hi2c, MPU6050_ADDR, REG_GYRO_XOUT_H, 1,
                              rec_data, 6, MPU6050_I2C_TIMEOUT) != HAL_OK) {
            /* Save whatever partial average we have so far and report
             * the failure — caller should re-run calibration.         */
            if (i > 0) {
                gyro_bias_x = (float)(sum_x / i);
                gyro_bias_y = (float)(sum_y / i);
                gyro_bias_z = (float)(sum_z / i);
            }
            return MPU6050_ERROR;
        }

        raw_gx = (int16_t)((rec_data[0] << 8) | rec_data[1]);
        raw_gy = (int16_t)((rec_data[2] << 8) | rec_data[3]);
        raw_gz = (int16_t)((rec_data[4] << 8) | rec_data[5]);

        sum_x += (float)raw_gx / 131.0f;
        sum_y += (float)raw_gy / 131.0f;
        sum_z += (float)raw_gz / 131.0f;

        HAL_Delay(2);
    }

    gyro_bias_x = (float)(sum_x / samples);
    gyro_bias_y = (float)(sum_y / samples);
    gyro_bias_z = (float)(sum_z / samples);

    return MPU6050_OK;
}

uint8_t MPU6050_Read_All(I2C_HandleTypeDef *hi2c, MPU6050_t *mpu, float dt)
{
    uint8_t raw_data[14];

    if (HAL_I2C_Mem_Read(hi2c, MPU6050_ADDR, REG_ACCEL_XOUT_H, 1,
                          raw_data, 14, MPU6050_I2C_TIMEOUT) != HAL_OK) {
        /* I2C failed this cycle — leave *mpu exactly as it was rather
         * than overwrite good data with zeros/garbage. Caller should
         * check the return value if it needs to know this happened.  */
        return MPU6050_ERROR;
    }

    int16_t raw_ax = (int16_t)((raw_data[0] << 8) | raw_data[1]);
    int16_t raw_ay = (int16_t)((raw_data[2] << 8) | raw_data[3]);
    int16_t raw_az = (int16_t)((raw_data[4] << 8) | raw_data[5]);

    mpu->Ax = (float)raw_ax / 16384.0f;
    mpu->Ay = (float)raw_ay / 16384.0f;
    mpu->Az = (float)raw_az / 16384.0f;

    int16_t raw_temp = (int16_t)((raw_data[6] << 8) | raw_data[7]);
    mpu->Temperature = ((float)raw_temp / 340.0f) + 36.53f;

    int16_t raw_gx = (int16_t)((raw_data[8] << 8) | raw_data[9]);
    int16_t raw_gy = (int16_t)((raw_data[10] << 8) | raw_data[11]);
    int16_t raw_gz = (int16_t)((raw_data[12] << 8) | raw_data[13]);

    mpu->Gx = ((float)raw_gx / 131.0f) - gyro_bias_x;
    mpu->Gy = ((float)raw_gy / 131.0f) - gyro_bias_y;
    mpu->Gz = ((float)raw_gz / 131.0f) - gyro_bias_z;

    /* Gravity-referenced pitch/roll from the accelerometer alone —
     * these do NOT drift over time, unlike gyro-only integration.     */
    float accel_pitch = atan2f(mpu->Ay, sqrtf(mpu->Ax * mpu->Ax + mpu->Az * mpu->Az)) * 57.29578f;
    float accel_roll  = atan2f(-mpu->Ax, mpu->Az) * 57.29578f;

    /* Complementary filter, 98% gyro / 2% accel.
     * NOTE: pitch/roll are currently NOT used by the application (the web
     * page computes its own wheel angle). Before relying on them, verify
     * the gyro/accel axis pairing on hardware: rotate about one axis and
     * confirm the matching gyro channel moves. */
    mpu->pitch = 0.98f * (mpu->pitch + mpu->Gy * dt) + 0.02f * accel_pitch;
    mpu->roll  = 0.98f * (mpu->roll  + mpu->Gx * dt) + 0.02f * accel_roll;

    return MPU6050_OK;
}
