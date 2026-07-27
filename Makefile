# Makefile for effchango - Chango musical instrument on Efficient E1x
#
# Targets:
#   sim        - Build for simulation (runs on host via effcc sim backend)
#   fabric     - Build for E1x fabric hardware (with UART3 printf)
#   pnrviz     - Build for fabric and generate PnR visualization SVGs
#   pnrviz_pdf - Generate PDFs from PnR SVGs
#   sdk        - Build the SDK libraries (libeff.a + drivers)
#   data       - Regenerate input data header
#   play       - Build sim and play audio output via SoX
#   clean      - Remove build artifacts

# Toolchain locations. effcc and friends live in a build tree, not on PATH.
EFFCC_BIN ?= /home/blucia/effcc_bld/bin
EFFCC ?= $(EFFCC_BIN)/effcc
FABRIC_TRANSLATE ?= $(EFFCC_BIN)/fabric-translate
# GNU objcopy, not the toolchain's llvm-objcopy: only binutils has the
# "verilog" writer that eff-flash's .hex input needs.
OBJCOPY ?= objcopy
SDK_ROOT = /home/blucia/cvsandbox/apps/eff_sdk
SDK_INC = $(SDK_ROOT)/include
STOPPROP_INC = /home/blucia/cvsandbox/apps/include

# Prebuilt SDK. Built for e1x with the default stdio port, which for E1X is
# already UART_3 / PINMUX_3 -- the same port STDIO_OPTS below selects.
SDK_BUILD = /home/blucia/eff_sdk_bld

INCLUDES = -I$(SDK_INC) -I$(STOPPROP_INC) -I.
CFLAGS = -O3 $(INCLUDES)

# Camera build mode:
#   HW_CAMERA=1     -> use HM0360 driver instead of the SWIL stub
#   CAMERA_DEBUG=1  -> stream raw camera frames over UART for view.py /
#                      grab_frames.py (skips audio synthesis in the main loop)
#   SAMPLES_PER_FRAME -> audio samples generated per camera frame (default 1920)
#   IMU=1           -> re-enable the synthetic IMU tilt sweep of the LPF (default off)
HW_CAMERA ?= 0
CAMERA_DEBUG ?= 0
SAMPLES_PER_FRAME ?=
IMU ?= 0

# The hardware path replaces main.c wholesale with main_hm0360.c, which keeps
# the known-good minimal-camera bring-up verbatim and splices chango into its
# capture loop. The SWIL path keeps main.c and the camera_t abstraction.
ifeq ($(HW_CAMERA),1)
  MAIN_SRC = main_hm0360.c
  CAM_SRC  = hm0360.c
  CAM_DEFS = -DUSE_HW_CAMERA
else
  MAIN_SRC = main.c
  CAM_SRC  = camera_swil.c
  CAM_DEFS =
endif

ifeq ($(CAMERA_DEBUG),1)
  CAM_DEFS += -DCAMERA_DEBUG
endif

# Capture mode: 1 = continuous streaming (default), 0 = donor snapshot path
CAM_STREAMING ?= 1
CAM_DEFS += -DCAM_STREAMING=$(CAM_STREAMING)

# Bring-up diagnostic: force the sensor's internal test pattern.
# 0x01 = colour bar, 0x21 = walking 1's. Empty = off.
CAM_TEST_PATTERN ?=
ifneq ($(CAM_TEST_PATTERN),)
  CAM_DEFS += -DCAM_TEST_PATTERN=$(CAM_TEST_PATTERN)
endif

# Diagnostic: emit the 4x4 intensity grid instead of audio (diag_intensity.py)
INTENSITY_DEBUG ?= 0
ifeq ($(INTENSITY_DEBUG),1)
  CAM_DEFS += -DINTENSITY_DEBUG
endif

ifneq ($(SAMPLES_PER_FRAME),)
  CAM_DEFS += -DSAMPLES_PER_FRAME=$(SAMPLES_PER_FRAME)
endif

ifeq ($(IMU),1)
  CAM_DEFS += -DUSE_IMU=1
endif

SOURCES = $(MAIN_SRC) chango.c $(CAM_SRC) audio_out_swil.c imu_swil.c
MAIN_OBJ_SIM = $(MAIN_SRC:.c=.sim.o)
CAM_OBJS_SIM = $(CAM_SRC:.c=.sim.o)
MAIN_OBJ_FAB = $(MAIN_SRC:.c=.fab.o)
CAM_OBJS_FAB = $(CAM_SRC:.c=.fab.o)
HEADERS = camera.h audio_out.h imu.h
DATA = chango_data.h.inc
PNRVIZ_DIR = pnrviz

# SDK libraries (prebuilt in $(SDK_BUILD); 'make sdk' reconfigures/rebuilds them)
LIBEFF = $(SDK_BUILD)/stdlib/libeff.a
LIBDRV_UART = $(SDK_BUILD)/drivers/uart/scalar/libeff_eff_drv_uart.a
LIBDRV_PINMUX = $(SDK_BUILD)/drivers/pinmux/scalar/libeff_eff_drv_pinmux.a
LIBDRV_GPIO = $(SDK_BUILD)/drivers/gpio/scalar/libeff_eff_drv_gpio.a
LIBDRV_I2C = $(SDK_BUILD)/drivers/i2c/scalar/libeff_eff_drv_i2c.a
LIBDRV_SPI = $(SDK_BUILD)/drivers/spi/scalar/libeff_eff_drv_spi.a

SDK_LIBS = $(LIBEFF) $(LIBDRV_UART) $(LIBDRV_PINMUX) $(LIBDRV_GPIO) $(LIBDRV_I2C) $(LIBDRV_SPI)

# UART port 3 for stdio on hardware
STDIO_OPTS = -DSTDIO_UART=UART_3 -DSTDIO_PINMUX=PINMUX_3

.PHONY: all sim fabric pnrviz pnrviz_pdf sdk data play clean

all: sim

# Generate input data header
data: $(DATA)

$(DATA): gen_data.py
	python3 gen_data.py -o $@

# Rebuild the prebuilt SDK in place (only needed after SDK source changes).
sdk:
	$(MAKE) -C $(SDK_BUILD) eff 2>&1 | tail -5
	@echo "[sdk] Built $(LIBEFF)"

$(LIBEFF):
	@echo "ERROR: SDK library not found at $@"
	@echo "       Expected a configured e1x SDK build tree at $(SDK_BUILD)."
	@echo "       Run 'make sdk' if that tree exists but is not built."
	@false

# Simulation build (runs on host, simulates E1x)
sim: chango_sim

SIM_FLAGS = --sim -c $(CFLAGS) -DEFF_ARCH_E1X -DSIM_BUILD $(CAM_DEFS) -flto

%.sim.o: %.c $(DATA) $(HEADERS)
	$(EFFCC) $(SIM_FLAGS) -o $@ $<

chango_sim: chango.sim.o $(CAM_OBJS_SIM) audio_out_swil.sim.o imu_swil.sim.o $(MAIN_OBJ_SIM)
	$(EFFCC) --sim -o $@ $^ \
		-DEFF_ARCH_E1X --target=e1x -flto -fuse-ld=lld

# Fabric build (for actual E1x hardware, links SDK for UART3 printf)
fabric: chango_fabric

FAB_FLAGS = -c $(CFLAGS) -DEFF_ARCH_E1X $(STDIO_OPTS) $(CAM_DEFS) -flto --target=e1x

%.fab.o: %.c $(DATA) $(HEADERS)
	$(EFFCC) $(FAB_FLAGS) -o $@ $<

chango_fabric: chango.fab.o $(CAM_OBJS_FAB) audio_out_swil.fab.o imu_swil.fab.o $(MAIN_OBJ_FAB) $(LIBEFF)
	$(EFFCC) -o $@ \
		chango.fab.o $(CAM_OBJS_FAB) audio_out_swil.fab.o imu_swil.fab.o $(MAIN_OBJ_FAB) \
		-Wl,--whole-archive $(SDK_LIBS) -Wl,--no-whole-archive \
		-DEFF_ARCH_E1X $(STDIO_OPTS) -flto --target=e1x -Wl,--allow-multiple-definition
	$(OBJCOPY) -Overilog $@ $@.hex

# Fabric build with PnR visualization (generates SVGs from placed-and-routed MLIR)
pnrviz: chango_fabric_pnrviz

chango_fabric_pnrviz: chango.fab.o $(CAM_OBJS_FAB) audio_out_swil.fab.o imu_swil.fab.o $(MAIN_OBJ_FAB) $(LIBEFF)
	mkdir -p $(PNRVIZ_DIR)
	$(EFFCC) -o chango_fabric \
		chango.fab.o $(CAM_OBJS_FAB) audio_out_swil.fab.o imu_swil.fab.o $(MAIN_OBJ_FAB) \
		-Wl,--whole-archive $(SDK_LIBS) -Wl,--no-whole-archive \
		-DEFF_ARCH_E1X $(STDIO_OPTS) -flto --target=e1x -Wl,--gc-sections -Wl,--allow-multiple-definition \
		--emit-pnrviz-mlir --pnrviz-folder=$(PNRVIZ_DIR)
	$(OBJCOPY) -Overilog chango_fabric chango_fabric.hex
	@echo "[pnrviz] Generating SVGs from PnR MLIR..."
	@for mlir_file in $(PNRVIZ_DIR)/*.mlir; do \
		if [ -f "$$mlir_file" ]; then \
			echo "  $$mlir_file"; \
			$(FABRIC_TRANSLATE) "$$mlir_file" --fabric-emit-viz; \
		fi; \
	done
	@echo "[pnrviz] Done. SVGs in $(PNRVIZ_DIR)/"

# Generate PDFs from PnR SVGs
pnrviz_pdf: pnrviz
	@echo "[pnrviz_pdf] Converting SVGs to PDFs..."
	@for svg in $(PNRVIZ_DIR)/*.svg; do \
		if [ -f "$$svg" ]; then \
			pdf="$${svg%.svg}.pdf"; \
			rsvg-convert -f pdf -o "$$pdf" "$$svg"; \
			echo "  $$(basename $$pdf)"; \
		fi; \
	done
	@echo "[pnrviz_pdf] Done. PDFs in $(PNRVIZ_DIR)/"

# Run sim, extract audio from printf output, and play via SoX
play: chango_sim
	./chango_sim | python3 uart_to_raw.py > chango_output.raw
	play -t raw -r 8000 -e signed -b 16 -c 1 chango_output.raw

clean:
	rm -f *.o *.sim.o *.fab.o chango_sim chango_fabric chango_fabric.hex chango_output.raw chango_data.h.inc chango_stderr.txt
	rm -rf $(PNRVIZ_DIR)
