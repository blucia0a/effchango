# Makefile for effchango - Chango musical instrument on Efficient E1x
#
# Targets:
#   sim    - Build for simulation (runs on host via effcc sim backend)
#   native - Build native host binary (for quick testing)
#   fabric - Build for E1x fabric hardware
#   data   - Regenerate input data header
#   play   - Build sim and play audio output via SoX
#   clean  - Remove build artifacts

EFFCC ?= effcc
SDK_INC = /home/blucia/cvsandbox/apps/eff_sdk/include
STOPPROP_INC = /home/blucia/cvsandbox/apps/include

INCLUDES = -I$(SDK_INC) -I$(STOPPROP_INC) -I.
CFLAGS = -O3 $(INCLUDES)

SOURCES = main.c chango.c camera_swil.c audio_out_swil.c imu_swil.c
DATA = chango_data.h.inc

.PHONY: all sim native fabric data play clean

all: sim

# Generate input data header
data: $(DATA)

$(DATA): gen_data.py
	python3 gen_data.py -o $@

# Simulation build (runs on host, simulates E1x)
sim: chango_sim

SIM_FLAGS = --sim -c $(CFLAGS) -DEFF_ARCH_E1X -DSIM_BUILD -flto

HEADERS = camera.h audio_out.h imu.h

chango_sim: $(SOURCES) $(DATA) $(HEADERS)
	$(EFFCC) $(SIM_FLAGS) -o chango.sim.o chango.c
	$(EFFCC) $(SIM_FLAGS) -o camera_swil.sim.o camera_swil.c
	$(EFFCC) $(SIM_FLAGS) -o audio_out_swil.sim.o audio_out_swil.c
	$(EFFCC) $(SIM_FLAGS) -o imu_swil.sim.o imu_swil.c
	$(EFFCC) $(SIM_FLAGS) -o main.sim.o main.c
	$(EFFCC) --sim -o $@ chango.sim.o camera_swil.sim.o audio_out_swil.sim.o imu_swil.sim.o main.sim.o -DEFF_ARCH_E1X --target=monaco -flto -fuse-ld=lld

# Fabric build (for actual E1x hardware)
fabric: chango_fabric

FAB_FLAGS = -c $(CFLAGS) -DEFF_ARCH_E1X -DHW_BUILD -flto --target=monaco

chango_fabric: $(SOURCES) $(DATA) $(HEADERS)
	$(EFFCC) $(FAB_FLAGS) -o chango.fab.o chango.c
	$(EFFCC) $(FAB_FLAGS) -o camera_swil.fab.o camera_swil.c
	$(EFFCC) $(FAB_FLAGS) -o audio_out_swil.fab.o audio_out_swil.c
	$(EFFCC) $(FAB_FLAGS) -o imu_swil.fab.o imu_swil.c
	$(EFFCC) $(FAB_FLAGS) -o main.fab.o main.c
	$(EFFCC) -o $@ chango.fab.o camera_swil.fab.o audio_out_swil.fab.o imu_swil.fab.o main.fab.o -DEFF_ARCH_E1X -DHW_BUILD -flto --target=monaco -Wl,--gc-sections

# Run sim, extract audio from printf output, and play via SoX
play: chango_sim
	./chango_sim | python3 uart_to_raw.py > chango_output.raw
	play -t raw -r 8000 -e signed -b 16 -c 1 chango_output.raw

clean:
	rm -f *.o chango_sim chango_fabric chango_output.raw chango_data.h.inc chango_stderr.txt
