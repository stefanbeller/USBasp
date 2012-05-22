#!/usr/bin/env python

import usb
import time
import sys

def getHandle():
    asp = None
    busses = usb.busses()
    for bus in busses:
        devices = bus.devices
        for dev in devices:
            if dev.idVendor == 0x16c0 and dev.idProduct==0x05dc:
                asp = dev #.open()
    if asp is None:
        return None
    return asp.open()

def enableSerialMode(USBaspHandle):
    USBaspHandle.controlMsg(128|32, 32, 8)

def disableSerialMode(USBaspHandle):
    USBaspHandle.controlMsg(128|32, 34, 8)

def getData(USBaspHandle):
    return USBaspHandle.controlMsg(128|32, 33, 8)

handle = getHandle()
if handle is None:
    sys.stderr.write("USBASP not found!\n")
    exit(1);

try:
    enableSerialMode(handle)
    while True:
        data = getData(handle)
        time.sleep(0.01)
        if len(data):
            for char in data:
                sys.stdout.write(chr(char))
            sys.stdout.flush()

except KeyboardInterrupt:
    disableSerialMode(handle)
