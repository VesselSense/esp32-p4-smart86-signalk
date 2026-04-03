#!/usr/bin/env python3
"""Capture serial output from device for N seconds, write to /tmp/serial_capture.txt."""
import serial
import time
import sys

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-0001"
duration = int(sys.argv[2]) if len(sys.argv) > 2 else 20

print(f"Capturing {duration}s from {port} → /tmp/serial_capture.txt")
ser = serial.Serial(port, 115200, timeout=1)
out = open("/tmp/serial_capture.txt", "w")
start = time.time()
while time.time() - start < duration:
    line = ser.readline().decode("utf-8", errors="replace").strip()
    if line:
        print(line)
        out.write(line + "\n")
        out.flush()
ser.close()
out.close()
print("Done.")
