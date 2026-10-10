CROSS ?= riscv64-unknown-elf-
VOICE_MIC_SD_GPIO ?= 5
VOICE_LCD ?= 1
VOICE_MOTOR_FULL_DUTY ?= 1
VOICE_LEFT_CHANNEL_A ?= 1
BUILD_DIR ?= build/voice
VOICE_MODEL_DIR ?= voice/models/mfcc-cmn-global-20261010-11
VOICE_FIRMWARE_VERSION ?= v9
VOICE_SELFTEST_HEADER ?= tests/generated/selftest_pcm.h
FLAGS := -march=rv32im_zicsr -mabi=ilp32 -O2 -flto -ffreestanding -fno-builtin \
         -fno-pic -fno-stack-protector -msmall-data-limit=0 -mno-relax \
         -ffunction-sections -fdata-sections -nostdlib -Wall -Wextra -Werror -I. \
         -DVOICE_MODEL_HEADER='"$(VOICE_MODEL_DIR)/model.h"' \
         -DVOICE_DSP_HEADER='"$(VOICE_MODEL_DIR)/dsp.h"' \
         -DVOICE_FIRMWARE_VERSION='"$(VOICE_FIRMWARE_VERSION)"' \
         -DVOICE_SELFTEST_HEADER='"$(VOICE_SELFTEST_HEADER)"'
DEFINES := -DVOICE_MIC_SD_GPIO=$(VOICE_MIC_SD_GPIO) -DVOICE_LCD=$(VOICE_LCD) \
           -DVOICE_MOTOR_FULL_DUTY=$(VOICE_MOTOR_FULL_DUTY) -DVOICE_LEFT_CHANNEL_A=$(VOICE_LEFT_CHANNEL_A)
SOURCE := start.s voice/soft_i2s.S main.c driver/voice_board.c driver/voice_audio_diag.c \
          voice/fixed.c voice/frontend.c voice/infer.c voice/motor.c
HEADERS := main.h board.h driver/voice_board.h voice/voice.h voice/motor.h \
           voice/model_config.h $(VOICE_MODEL_DIR)/model.h $(VOICE_MODEL_DIR)/dsp.h
LINK := -Wl,-T,sections.lds,--no-relax,--gc-sections

.PHONY: all model car diagnostic selftest capturetest triggeredtest
all: model
model: $(BUILD_DIR)/c1_voice_model.bin
car: $(BUILD_DIR)/c1_voice_car.bin
diagnostic: $(BUILD_DIR)/c1_voice_diagnostic.bin
selftest: $(BUILD_DIR)/selftest.bin
capturetest: $(BUILD_DIR)/capturetest.bin
triggeredtest: $(BUILD_DIR)/triggeredtest.bin

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/c1_voice_model.elf: $(SOURCE) $(HEADERS) sections.lds Makefile | $(BUILD_DIR)
	$(CROSS)gcc $(FLAGS) $(DEFINES) -DVOICE_DRIVE=0 $(LINK),-Map,$(@:.elf=.map) $(SOURCE) -lgcc -o $@

$(BUILD_DIR)/c1_voice_car.elf: $(SOURCE) $(HEADERS) sections.lds Makefile | $(BUILD_DIR)
	$(CROSS)gcc $(FLAGS) $(DEFINES) -DVOICE_DRIVE=1 $(LINK),-Map,$(@:.elf=.map) $(SOURCE) -lgcc -o $@

$(BUILD_DIR)/selftest.elf: $(SOURCE) $(HEADERS) sections.lds Makefile $(VOICE_SELFTEST_HEADER) | $(BUILD_DIR)
	$(CROSS)gcc $(FLAGS) -DVOICE_MIC_SD_GPIO=$(VOICE_MIC_SD_GPIO) -DVOICE_LCD=0 -DVOICE_DRIVE=0 -DVOICE_SELFTEST $(LINK),-Map,$(@:.elf=.map) $(SOURCE) -lgcc -o $@

$(BUILD_DIR)/capturetest.elf: start.s voice/soft_i2s.S tests/capture_main.c driver/voice_board.c $(HEADERS) sections.lds Makefile | $(BUILD_DIR)
	$(CROSS)gcc $(FLAGS) -DVOICE_MIC_SD_GPIO=$(VOICE_MIC_SD_GPIO) -DVOICE_LCD=0 -DVOICE_DRIVE=0 $(LINK),-Map,$(@:.elf=.map) start.s voice/soft_i2s.S tests/capture_main.c driver/voice_board.c -lgcc -o $@

$(BUILD_DIR)/triggeredtest.elf: start.s voice/soft_i2s.S tests/triggered_capture.c driver/voice_board.c $(HEADERS) sections.lds Makefile | $(BUILD_DIR)
	$(CROSS)gcc $(FLAGS) -DVOICE_MIC_SD_GPIO=$(VOICE_MIC_SD_GPIO) -DVOICE_LCD=0 -DVOICE_DRIVE=0 $(LINK),-Map,$(@:.elf=.map) start.s voice/soft_i2s.S tests/triggered_capture.c driver/voice_board.c -lgcc -o $@

$(BUILD_DIR)/c1_voice_diagnostic.elf: start.s voice/soft_i2s.S diagnostics/board_main.c driver/voice_board.c voice/motor.c $(HEADERS) sections.lds Makefile | $(BUILD_DIR)
	$(CROSS)gcc $(FLAGS) -DVOICE_MIC_SD_GPIO=$(VOICE_MIC_SD_GPIO) -DVOICE_LCD=0 -DVOICE_DRIVE=1 $(LINK),-Map,$(@:.elf=.map) start.s voice/soft_i2s.S diagnostics/board_main.c driver/voice_board.c voice/motor.c -lgcc -o $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(CROSS)objcopy -O binary $< $@
	$(CROSS)objdump -d $< > $(@:.bin=.dis)
	$(CROSS)size $<
	sha256sum $@

.SECONDARY:
