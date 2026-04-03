/*
 * camera_swil.c - Software-in-the-loop camera implementation
 *
 * Returns a static test image on every capture(). Used for testing
 * the pipeline without camera hardware.
 */

#include "camera.h"

typedef struct {
    const uint8_t *test_image;
} camera_swil_ctx_t;

/* Single static context — no dynamic allocation needed on embedded. */
static camera_swil_ctx_t swil_ctx;

static int swil_init(camera_t *cam) {
    (void)cam;
    return 0;
}

static const uint8_t *swil_capture(camera_t *cam) {
    camera_swil_ctx_t *ctx = (camera_swil_ctx_t *)cam->ctx;
    return ctx->test_image;
}

static void swil_shutdown(camera_t *cam) {
    (void)cam;
}

camera_t camera_swil_create(const uint8_t *test_image, int width, int height) {
    swil_ctx.test_image = test_image;

    camera_t cam;
    cam.width = width;
    cam.height = height;
    cam.init = swil_init;
    cam.capture = swil_capture;
    cam.shutdown = swil_shutdown;
    cam.ctx = &swil_ctx;
    return cam;
}
