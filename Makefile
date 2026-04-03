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

SOURCES = main.c chango.c
DATA = chango_data.h.inc

.PHONY: all sim native fabric data play clean

all: sim

# Generate input data header
data: $(DATA)

$(DATA): gen_data.py
	python3 gen_data.py -o $@

# Simulation build (runs on host, simulates E1x)
sim: chango_sim

chango_sim: $(SOURCES) $(DATA)
	$(EFFCC) --sim -c $(CFLAGS) -DEFF_ARCH_E1X -DSIM_BUILD -flto -o chango.sim.o chango.c
	$(EFFCC) --sim -c $(CFLAGS) -DEFF_ARCH_E1X -DSIM_BUILD -flto -o main.sim.o main.c
	$(EFFCC) --sim -o $@ chango.sim.o main.sim.o -DEFF_ARCH_E1X --target=monaco -flto -fuse-ld=lld

# Fabric build (for actual E1x hardware)
fabric: chango_fabric

chango_fabric: $(SOURCES) $(DATA)
	$(EFFCC) -c $(CFLAGS) -DEFF_ARCH_E1X -DHW_BUILD -flto --target=monaco -o chango.fab.o chango.c
	$(EFFCC) -c $(CFLAGS) -DEFF_ARCH_E1X -DHW_BUILD -flto --target=monaco -o main.fab.o main.c
	$(EFFCC) -o $@ chango.fab.o main.fab.o -DEFF_ARCH_E1X -DHW_BUILD -flto --target=monaco -Wl,--gc-sections

# Run sim and play audio via SoX
play: chango_sim
	./chango_sim
	play -t raw -r 8000 -e signed -b 16 -c 1 chango_output.raw

clean:
	rm -f *.o chango_sim chango_fabric chango_output.raw chango_data.h.inc chango_stderr.txt
