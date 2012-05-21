#!/usr/bin/env python

import usb
import time

asp = None
busses = usb.busses()
for bus in busses:
    devices = bus.devices
    for dev in devices:
        #print "Device:", dev.filename
        #print "  idVendor: %d (0x%04x)" % (dev.idVendor, dev.idVendor)
        #print "  idProduct: %d (0x%04x)" % (dev.idProduct, dev.idProduct)
        if dev.idVendor == 0x16c0 and dev.idProduct==0x05dc:
            asp = dev #.open()

if asp is None:
    print "USBASP not found!"
    exit(1)

handle = asp.open()

def enableSerialMode(USBaspHandle):
    USBaspHandle.controlMsg(128|32, 32, 8)

def disableSerialMode(USBaspHandle):
    USBaspHandle.controlMsg(128|32, 34, 8)

def getData(USBaspHandle):
    return USBaspHandle.controlMsg(128|32, 33, 8)

try:
    enableSerialMode(handle)
    while True:
        data = getData(handle)
        time.sleep(0.01)
        if len(data):
            print data

except KeyboardInterrupt:
    disableSerialMode(handle)
