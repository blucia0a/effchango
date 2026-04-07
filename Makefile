# Makefile for effchango - Chango musical instrument on Efficient E1x
#
# Targets:
#   sim    - Build for simulation (runs on host via effcc sim backend)
#   fabric - Build for E1x fabric hardware
#   pnrviz - Build for fabric and generate PnR visualization SVGs
#   data   - Regenerate input data header
#   play   - Build sim and play audio output via SoX
#   clean  - Remove build artifacts

EFFCC ?= effcc
FABRIC_TRANSLATE ?= fabric-translate
SDK_INC = /home/blucia/cvsandbox/apps/eff_sdk/include
STOPPROP_INC = /home/blucia/cvsandbox/apps/include

INCLUDES = -I$(SDK_INC) -I$(STOPPROP_INC) -I.
CFLAGS = -O3 $(INCLUDES)

SOURCES = main.c chango.c camera_swil.c audio_out_swil.c imu_swil.c
HEADERS = camera.h audio_out.h imu.h
DATA = chango_data.h.inc
PNRVIZ_DIR = pnrviz

.PHONY: all sim fabric pnrviz pnrviz_pdf data play clean

all: sim

# Generate input data header
data: $(DATA)

$(DATA): gen_data.py
	python3 gen_data.py -o $@

# Simulation build (runs on host, simulates E1x)
sim: chango_sim

SIM_FLAGS = --sim -c $(CFLAGS) -DEFF_ARCH_E1X -DSIM_BUILD -flto

chango_sim: $(SOURCES) $(DATA) $(HEADERS)
	$(EFFCC) $(SIM_FLAGS) -o chango.sim.o chango.c
	$(EFFCC) $(SIM_FLAGS) -o camera_swil.sim.o camera_swil.c
	$(EFFCC) $(SIM_FLAGS) -o audio_out_swil.sim.o audio_out_swil.c
	$(EFFCC) $(SIM_FLAGS) -o imu_swil.sim.o imu_swil.c
	$(EFFCC) $(SIM_FLAGS) -o main.sim.o main.c
	$(EFFCC) --sim -o $@ chango.sim.o camera_swil.sim.o audio_out_swil.sim.o imu_swil.sim.o main.sim.o \
		-DEFF_ARCH_E1X --target=e1x -flto -fuse-ld=lld

# Fabric build (for actual E1x hardware)
fabric: chango_fabric

FAB_FLAGS = -c $(CFLAGS) -DEFF_ARCH_E1X -flto --target=e1x

chango_fabric: $(SOURCES) $(DATA) $(HEADERS)
	$(EFFCC) $(FAB_FLAGS) -o chango.fab.o chango.c
	$(EFFCC) $(FAB_FLAGS) -o camera_swil.fab.o camera_swil.c
	$(EFFCC) $(FAB_FLAGS) -o audio_out_swil.fab.o audio_out_swil.c
	$(EFFCC) $(FAB_FLAGS) -o imu_swil.fab.o imu_swil.c
	$(EFFCC) $(FAB_FLAGS) -o main.fab.o main.c
	$(EFFCC) -o $@ chango.fab.o camera_swil.fab.o audio_out_swil.fab.o imu_swil.fab.o main.fab.o \
		-DEFF_ARCH_E1X -flto --target=e1x -Wl,--gc-sections

# Fabric build with PnR visualization (generates SVGs from placed-and-routed MLIR)
pnrviz: chango_fabric_pnrviz

chango_fabric_pnrviz: $(SOURCES) $(DATA) $(HEADERS)
	mkdir -p $(PNRVIZ_DIR)
	$(EFFCC) $(FAB_FLAGS) -o chango.fab.o chango.c
	$(EFFCC) $(FAB_FLAGS) -o camera_swil.fab.o camera_swil.c
	$(EFFCC) $(FAB_FLAGS) -o audio_out_swil.fab.o audio_out_swil.c
	$(EFFCC) $(FAB_FLAGS) -o imu_swil.fab.o imu_swil.c
	$(EFFCC) $(FAB_FLAGS) -o main.fab.o main.c
	$(EFFCC) -o chango_fabric chango.fab.o camera_swil.fab.o audio_out_swil.fab.o imu_swil.fab.o main.fab.o \
		-DEFF_ARCH_E1X -flto --target=e1x -Wl,--gc-sections \
		--emit-pnrviz-mlir --pnrviz-folder=$(PNRVIZ_DIR)
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
	rm -f *.o chango_sim chango_fabric chango_output.raw chango_data.h.inc chango_stderr.txt
	rm -rf $(PNRVIZ_DIR)
