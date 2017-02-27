#
#   Makefile for usbasp
#   20061119   Thomas Fischl        original
#   20061120   Hanns-Konrad Unger   help: and TARGET=atmega48 added
#

# TARGET=atmega8    HFUSE=0xc9  LFUSE=0xef
# TARGET=atmega48   HFUSE=0xdd  LFUSE=0xff
# TARGET=atmega88   HFUSE=0xdd  LFUSE=0xff
# TARGET=at90s2313
ifndef TARGET
	TARGET=atmega8
endif

ifeq ($(TARGET),atmega8)
    HFUSE=0xc9
    LFUSE=0xef
endif
ifeq ($(TARGET),atmega48)
    HFUSE=0xdd
    LFUSE=0xff
endif
ifeq ($(TARGET),atmega88)
    HFUSE=0xdd
    LFUSE=0xff
endif
ifeq ($(TARGET),at90s2313)
    HFUSE=0xc9
    LFUSE=0xef
endif




# ISP=bsd      PORT=/dev/parport0
# ISP=ponyser  PORT=/dev/ttyS1
# ISP=stk500   PORT=/dev/ttyS1
# ISP=usbasp   PORT=/dev/usb/ttyUSB0
# ISP=stk500v2 PORT=/dev/ttyUSB0
# ISP=avrisp   PORT=/dev/ttyUSB0 -BITRATE=-b 19200
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


help:
	@echo "Usage: make                same as make help"
	@echo "       make help           same as make"
	@echo "       [ISP=xxx] [TARGET=yyy] [PORT=/dev/tttt] make main.hex       create main.hex"
	@echo "       make clean          remove redundant data"
	@echo "       make disasm         disasm main"
	@echo "       [ISP=xxx] [TARGET=yyy] [PORT=/dev/tttt] make flash          upload main.hex into flash"
	@echo "       [ISP=xxx] [TARGET=yyy] [PORT=/dev/tttt] make fuses          program fuses"
	@echo "       make avrdude        test avrdude"
	@echo "Current values:"
	@echo "       TARGET=${TARGET}"
	@echo "       LFUSE=${LFUSE}"
	@echo "       HFUSE=${HFUSE}"
	@echo "       CLOCK=12000000"
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
	@echo "       at90s2313"
	

all: main.hex
	make flash

COMPILE = avr-gcc -Wall -O2 -Iusbdrv -I. -mmcu=$(TARGET) # -DDEBUG_LEVEL=2

OBJECTS = usbdrv/usbdrv.o usbdrv/usbdrvasm.o usbdrv/oddebug.o isp.o clock.o tpi.o main.o

.c.o:
	$(COMPILE) -c $< -o $@
#-Wa,-ahlms=$<.lst

.S.o:
	$(COMPILE) -x assembler-with-cpp -c $< -o $@
# "-x assembler-with-cpp" should not be necessary since this is the default
# file type for the .S (with capital S) extension. However, upper case
# characters are not always preserved on Windows. To ensure WinAVR
# compatibility define the file type manually.

.c.s:
	$(COMPILE) -S $< -o $@

clean:
	rm -f main.hex main.lst main.obj main.cof main.list main.map main.eep.hex main.bin *.o main.s usbdrv/*.o

# file targets:
main.bin:	$(OBJECTS)
	$(COMPILE) -o main.bin $(OBJECTS) -Wl,-Map,main.map

main.hex:	main.bin
	rm -f main.hex main.eep.hex
	avr-objcopy -j .text -j .data -O ihex main.bin main.hex
#	./checksize main.bin
# do the checksize script as our last action to allow successful compilation
# on Windows with WinAVR where the Unix commands will fail.

disasm:	main.bin
	avr-objdump -d main.bin

cpp:
	$(COMPILE) -E main.c

flash:
	avrdude -c ${ISP} -p ${TARGET} -P ${_PORT} $(BITRATE) -U flash:w:main.hex

fuses:
	avrdude -c ${ISP} -p ${TARGET} -P ${_PORT} $(BITRATE) -u -U hfuse:w:$(HFUSE):m -U lfuse:w:$(LFUSE):m

avrdude:
	avrdude -c ${ISP} -p ${TARGET} -P ${_PORT} $(BITRATE) -v

# Fuse atmega8 high byte HFUSE:
# 0xc9 = 1 1 0 0   1 0 0 1 <-- BOOTRST (boot reset vector at 0x0000)
#        ^ ^ ^ ^   ^ ^ ^------ BOOTSZ0
#        | | | |   | +-------- BOOTSZ1
#        | | | |   + --------- EESAVE (don't preserve EEPROM over chip erase)
#        | | | +-------------- CKOPT (full output swing)
#        | | +---------------- SPIEN (allow serial programming)
#        | +------------------ WDTON (WDT not always on)
#        +-------------------- RSTDISBL (reset pin is enabled)
# Fuse atmega8 low byte LFUSE:
# 0x9f = 1 0 0 1   1 1 1 1
#        ^ ^ \ /   \--+--/
#        | |  |       +------- CKSEL 3..0 (external >8M crystal)
#        | |  +--------------- SUT 1..0 (crystal osc, BOD enabled)
#        | +------------------ BODEN (BrownOut Detector enabled)
#        +-------------------- BODLEVEL (2.7V)
#
# Fuse atmega48 high byte hfuse:
# 0xdf = 1 1 0 1   1 1 1 1     factory setting
#        ^ ^ ^ ^   ^ \-+-/
#        | | | |   |   +------ BODLEVEL (Brown out disabled)
#        | | | |   + --------- EESAVE (don't preserve EEPROM over chip erase)
#        | | | +-------------- WDTON (WDT not always on)
#        | | +---------------- SPIEN (allow serial programming)
#        | +------------------ DWEN (debug wire is disabled)
#        +-------------------- RSTDISBL (reset pin is enabled)
# 0xdd = ext.reset, no DW, SPI, no watchdog, no save eeprom, BOD 2.7V
# Fuse atmega48 low byte lfuse:
# 0x62 = 0 1 1 0   0 0 1 0     factory setting
#        ^ ^ \ /   \--+--/
#        | |  |       +------- CKSEL 3..0 (internal 8Mhz Oszillator)
#        | |  +--------------- SUT 1..0 (start-up time)
#        | +------------------ CKOUT (no clock output)
#        +-------------------- CKDIV8 (divide clock by 8)
# 0xdc = divide/1,no clock output,fast raising power,low Pw Oszil. 3..8 Mhz
# 0xe0 = divide/1,no clock output,fast raising power,external Oszil.
# 0xff = divide/1,no clock output,slow raising power,low Pw Oszil 8..  Mhz


SERIAL = `echo /dev/tty.USA19QI*`
UISP = uisp -dprog=$S -dserial=$(SERIAL) -dpart=auto
# The two lines above are for "uisp" and the AVR910 serial programmer connected
# to a Keyspan USB to serial converter to a Mac running Mac OS X.
# Choose your favorite programmer and interface.

uisp:	all
	$(UISP) --erase
	$(UISP) --upload --verify if=main.hex

