#
#   Makefile for usbasp
#   20061119   Thomas Fischl        original
#   20061120   Hanns-Konrad Unger   help: and TARGET=atmega48 added
#

# crystal frequency
#F_CPU=12000000UL
#F_CPU=16000000UL

# TARGET=atmega8    HFUSE=0xc9  LFUSE=0xef
# TARGET=atmega48   HFUSE=0xdd  LFUSE=0xff
# TARGET=at90s2313
TARGET=atmega8
CFLAGS += -DF_CPU=12000000UL
HFUSE=0xc9
LFUSE=0xef

# Добавьте в начало Makefile размеры памяти для разных микроконтроллеров
ifeq ($(TARGET), atmega8)
  FLASH_SIZE = 8192
  RAM_SIZE = 1024
  EEPROM_SIZE = 512
else ifeq ($(TARGET), atmega328p)
  FLASH_SIZE = 32768
  RAM_SIZE = 2048
  EEPROM_SIZE = 1024
else
  FLASH_SIZE = 8192
  RAM_SIZE = 1024
  EEPROM_SIZE = 512
endif



# ISP=bsd      PORT=/dev/parport0
# ISP=ponyser  PORT=/dev/ttyS1
# ISP=stk500   PORT=/dev/ttyS1
# ISP=usbasp   PORT=/dev/usb/ttyUSB0
# ISP=stk500v2 PORT=/dev/ttyUSB0
#PORT=/dev/usb/ttyUSB0
PORT=usb

help:
	@echo "Usage: make                same as make help"
	@echo "       make help           same as make"
	@echo "       make main.hex       create main.hex"
	@echo "       make clean          remove redundant data"
	@echo "       make disasm         disasm main"
	@echo "       make flash          upload main.hex into flash"
	@echo "       make fuses          program fuses"
	@echo "       make avrdude        test avrdude"
	@echo "Current values:"
	@echo "       TARGET=${TARGET}"
	@echo "       LFUSE=${LFUSE}"
	@echo "       HFUSE=${HFUSE}"
	@echo "       CLOCK=12000000"
	@echo "       ISP=${ISP}"
	@echo "       PORT=${PORT}"

SRC += tpi.S
#CFLAGS += -fexec-charset=CP866 -finput-charset=UTF-8

#COMPILE = avr-gcc -Wall -Wextra -Os -flto -std=gnu99 -Iusbdrv -I. -mmcu=$(TARGET) # -DDEBUG_LEVEL=2
COMPILE = avr-gcc -Wall -Wextra -Os -std=gnu99 -Iusbdrv -I. -mmcu=$(TARGET) -ffunction-sections -fdata-sections -Wl,--gc-sections
COMPILE += -fno-jump-tables -fno-tree-switch-conversion -funsigned-char -fpack-struct

OBJECTS = usbdrv/usbdrv.o usbdrv/usbdrvasm.o usbdrv/oddebug.o isp.o clock.o tpi.o main.o i2c.o microwire.o 

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
	$(COMPILE) -o main.bin $(OBJECTS) -Wl,-Map,main.map -Wl,--relax

main.hex:	main.bin
	rm -f main.hex main.eep.hex
	avr-objcopy -j .text -j .data -O ihex main.bin main.hex
	@echo ""
	@echo "--- Memory Usage for $(TARGET) ---"	
	@echo "File sizes:"
	@echo "  ELF: `wc -c < main.bin` bytes (with debug info)"
	@echo "  HEX: `wc -c < main.hex` bytes (program file)"
	@echo ""
	@echo "Microcontroller memory usage:"
	@avr-size -A main.bin > _size.tmp
	@awk 'BEGIN {flash=0; ram=0; eeprom=0} \
	/\.text/ {flash += $$2} \
	/\.data/ {flash += $$2; ram += $$2} \
	/\.bss/  {ram += $$2} \
	/\.eeprom/ {eeprom += $$2} \
	END { \
	printf("%d %d %d\n", flash, ram, eeprom); \
  	}' _size.tmp | \
	while read flash ram eeprom; do \
  	flash_pct=$$(echo "scale=1; $$flash * 100 / $(FLASH_SIZE)" | bc); \
  	ram_pct=$$(echo "scale=1; $$ram * 100 / $(RAM_SIZE)" | bc); \
  	eeprom_pct=$$(echo "scale=1; $$eeprom * 100 / $(EEPROM_SIZE)" | bc); \
  	\
  	# Flash color \
  	if [ $$(echo "$$flash_pct < 70" | bc -l) -eq 1 ]; then \
    	flash_color="\033[0;32m"; \
  	elif [ $$(echo "$$flash_pct < 85" | bc -l) -eq 1 ]; then \
    	flash_color="\033[1;33m"; \
	 else \
    	flash_color="\033[0;31m"; \
  	fi; \
  	\
  	# RAM color \
	if [ $$(echo "$$ram_pct < 70" | bc -l) -eq 1 ]; then \
 	ram_color="\033[0;32m"; \
  	elif [ $$(echo "$$ram_pct < 85" | bc -l) -eq 1 ]; then \
    	ram_color="\033[1;33m"; \
  	else \
    	ram_color="\033[0;31m"; \
  	fi; \
  	\
  	# EEPROM color \
  	if [ $$(echo "$$eeprom_pct < 70" | bc -l) -eq 1 ]; then \
    	eeprom_color="\033[0;32m"; \
  	elif [ $$(echo "$$eeprom_pct < 85" | bc -l) -eq 1 ]; then \
    	eeprom_color="\033[1;33m"; \
  	else \
   	eeprom_color="\033[0;32m"; \
  	fi; \
  	\
  	printf "  Flash:  $${flash_color}%5d / %4d bytes (%5.1f%%)$$\033[0m\n" $$flash $(FLASH_SIZE) $$flash_pct; \
  	printf "  RAM:    $${ram_color}%5d / %4d bytes (%5.1f%%)$$\033[0m\n" $$ram $(RAM_SIZE) $$ram_pct; \
  	printf "  EEPROM: $${eeprom_color}%5d / %4d bytes (%5.1f%%)$$\033[0m\n" $$eeprom $(EEPROM_SIZE) $$eeprom_pct; \
	done
	@echo ""
	@echo "Section details:"
	@awk '/\.(text|data|bss|eeprom)/ {printf("  %-10s %6d bytes\n", $$1, $$2)}' _size.tmp
	@rm -f _size.tmp
	@echo "----------------------------------"

#	./checksize main.bin
# do the checksize script as our last action to allow successful compilation
# on Windows with WinAVR where the Unix commands will fail.

disasm:	main.bin
	avr-objdump -d main.bin

cpp:
	$(COMPILE) -E main.c

flash:   main.hex
	avrdude -c ${ISP} -p ${TARGET} -P ${PORT} -U flash:w:main.hex

fuses:
	avrdude -c ${ISP} -p ${TARGET} -P ${PORT} -u -U hfuse:w:$(HFUSE):m -U lfuse:w:$(LFUSE):m

avrdude:
	avrdude -c ${ISP} -p ${TARGET} -P ${PORT} -v

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

