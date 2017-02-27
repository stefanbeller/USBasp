ifndef TARGET
	TARGET=atmega8
endif

ifeq ($(TARGET),atmega8)
    HFUSE=0xc9
    LFUSE=0xef
    FILE=usbasp.atmega8.2017-02-27.hex
endif
ifeq ($(TARGET),atmega48)
    HFUSE=0xdd
    LFUSE=0xff
    FILE=usbasp.atmega48.2017-02-27.hex
endif
ifeq ($(TARGET),atmega88)
    HFUSE=0xdd
    LFUSE=0xff
    FILE=usbasp.atmega88.2017-02-27.hex
endif


ifndef ISP
	ISP=usbasp
endif

ifeq ($(ISP),bsd)
    _PORT=/dev/parport0
endif
ifeq ($(ISP),ponyser)
    _PORT=/dev/ttyS1
endif
ifeq ($(ISP),stk500)
    _PORT=/dev/ttyS1
endif
ifeq ($(ISP),usbasp)
    _PORT=/dev/usb/ttyUSB0
endif
ifeq ($(ISP),stk500v2)
    _PORT=/dev/ttyUSB0
endif
ifeq ($(ISP),avrisp)
    _PORT=/dev/ttyUSB0
    _BITRATE=-b 19200
endif

ifndef PORT
	PORT=$(_PORT)
endif

ifndef BITRATE
	BITRATE=$(_BITRATE)
endif


FLASHER        = avrdude -c $(ISP) -p $(TARGET) -P $(PORT) $(BITRATE)

help:
	@echo "[TARGET=xxx] [ISP=yyy] [PORT=/dev/tttt] make usbasp"
	@echo "             [ISP=yyy] [PORT=/dev/tttt] make shell"
	@echo "Current values:"
	@echo "       TARGET=${TARGET}"
	@echo "       LFUSE=${LFUSE}"
	@echo "       HFUSE=${HFUSE}"
	@echo "       ISP=${ISP}"
	@echo "       PORT=${PORT}"
	@echo "Possible values for ISP:"
	@echo "       bsd"
	@echo "       ponyser"
	@echo "       stk500"
	@echo "       usbasp"
	@echo "       stk500v2"
	@echo "       avrisp (through arduino see https://www.arduino.cc/en/Tutorial/ArduinoISP)"
	@echo "Possible values for TARGET:"
	@echo "       atmega8"
	@echo "       atmega48"
	@echo "       atmega88"

usbasp: fuses
	$(FLASHER) -B 1 -U flash:w:$(FILE)

fuses:
	$(FLASHER) -B 200 -U hfuse:w:$(HFUSE):m -U lfuse:w:$(LFUSE):m
shell:
	$(FLASHER) -t

