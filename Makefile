FQBN      = ATTinyCore:avr:attinyx5:chip=85,clock=8internal
BUILD_DIR = /tmp/tinytach_build
HEX       = $(BUILD_DIR)/TinyTach.ino.hex
MCU       = attiny85
PROGRAMMER = usbasp

all: upload

compile:
	arduino-cli compile --fqbn $(FQBN) --build-path $(BUILD_DIR) TinyTach

upload: compile
	avrdude -c $(PROGRAMMER) -p $(MCU) -B 32 -U flash:w:$(HEX):i

fuses:
	avrdude -c $(PROGRAMMER) -p $(MCU) -B 32 \
		-U lfuse:w:0xE2:m \
		-U hfuse:w:0xDF:m \
		-U efuse:w:0xFF:m

clean:
	arduino-cli compile --fqbn $(FQBN) --clean TinyTach

.PHONY: all compile upload fuses clean
