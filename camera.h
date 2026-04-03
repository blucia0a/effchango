/*
 * camera.h - Camera capture interface for effchango
 *
 * Abstracts frame acquisition so the pipeline can work with either
 * a software-in-the-loop test image or a real hardware camera driver.
 *
 * Each implementation provides init/capture/shutdown callbacks via
 * the camera_t struct. The pipeline calls capture() each frame to
 * get a pointer to a grayscale image buffer.
 */

#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>

typedef struct camera camera_t;

struct camera {
    int width;
    int height;

    /* Initialize the camera. Returns 0 on success. */
    int (*init)(camera_t *cam);

    /*
     * Capture a frame. Returns pointer to a width*height grayscale buffer.
     * The buffer is owned by the camera and valid until the next capture()
     * or shutdown(). Returns NULL on failure.
     */
    const uint8_t *(*capture)(camera_t *cam);

    /* Release resources. */
    void (*shutdown)(camera_t *cam);

    /* Implementation-private context. */
    void *ctx;
};

/*
 * Software-in-the-loop camera: returns the same static test image
 * every frame. No hardware required.
 *
 * test_image: pointer to width*height grayscale pixel data
 * width, height: image dimensions
 */
camera_t camera_swil_create(const uint8_t *test_image, int width, int height);

#endif /* CAMERA_H */
