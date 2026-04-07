/*
 * imu.h - IMU (Inertial Measurement Unit) interface for effchango
 *
 * Abstracts IMU reading so the pipeline can work with either a
 * software-in-the-loop synthetic signal or a real hardware IMU driver.
 *
 * IMU samples provide accelerometer and gyroscope data that can be
 * mapped to synthesis control parameters (e.g., LPF cutoff/resonance).
 */

#ifndef IMU_H
#define IMU_H

#include <stdint.h>

typedef struct {
    int16_t accel_x;    /* accelerometer X, milli-g (-1000 to +1000 typical) */
    int16_t accel_y;    /* accelerometer Y, milli-g */
    int16_t accel_z;    /* accelerometer Z, milli-g */
    int16_t gyro_x;     /* gyroscope X, milli-deg/s */
    int16_t gyro_y;     /* gyroscope Y, milli-deg/s */
    int16_t gyro_z;     /* gyroscope Z, milli-deg/s */
} imu_sample_t;

typedef struct imu imu_t;

struct imu {
    /* Initialize the IMU. Returns 0 on success. */
    int (*init)(imu_t *imu);

    /*
     * Read one sample from the IMU. Returns 0 on success.
     * Called once per frame in the main loop.
     */
    int (*read)(imu_t *imu, imu_sample_t *sample);

    /* Release resources. */
    void (*shutdown)(imu_t *imu);

    /* Implementation-private context. */
    void *ctx;
};

/*
 * Software-in-the-loop IMU: generates a synthetic sweep pattern.
 * accel_x sweeps -1000..+1000 milli-g over sweep_frames frames (one cycle),
 * simulating a slow tilt. accel_y sweeps at half the rate.
 * Useful for hearing the LPF cutoff/resonance sweep in the output.
 *
 * sweep_frames: number of frames for one full sweep cycle
 */
imu_t imu_swil_create(int sweep_frames);

#endif /* IMU_H */
