#!/usr/bin/env python3
# ==============================================================================
# 📡 POLITICIAN: PCAPNG Serial Listener Handler
# ==============================================================================
# Run this on your Laptop! It natively opens the USB port, grabs the raw binary
# PCAPNG blocks injected by the ESP32, and mathematically assembles them into a
# perfectly compliant `capture.pcapng` file that Hashcat can crack directly!
#
# Usage: python3 listener.py /dev/ttyUSB0
# ==============================================================================

import serial
import sys
import os

if len(sys.argv) < 2:
    print("Usage: python3 listener.py <PORT>")
    print("Example: python3 listener.py /dev/ttyUSB0")
    print("Example: python3 listener.py COM3")
    sys.exit(1)

SERIAL_PORT = sys.argv[1]
BAUD_RATE = 921600   # Must explicitly match the setup() inside SerialStreaming.ino!
OUTPUT_FILE = "capture.pcapng"

print(f"[*] Attaching Politician Listener to {SERIAL_PORT} @ {BAUD_RATE} baud...")

try:
    # Open Serial Port in raw binary mode
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=None)
except Exception as e:
    print(f"[-] FATAL: Failed to open port {SERIAL_PORT}.")
    print(f"    Error: {e}")
    sys.exit(1)

# Ensure fresh extraction file
if os.path.exists(OUTPUT_FILE):
    print(f"[*] Removing old {OUTPUT_FILE}...")
    os.remove(OUTPUT_FILE)

print(f"[+] Listener Online. Writing mathematically raw byte streams into {OUTPUT_FILE}...")
print("[!] Press CTRL+C at any time to safely terminate the stream.\n")

MAGIC_NUMBER = b'\n\r\r\n'
synced = False
buffer = b''

try:
    with open(OUTPUT_FILE, "wb") as f:
        while True:
            # The ESP32 flushes its exact EPB, SHB, and IDB byte arrays natively. 
            if ser.in_waiting > 0:
                chunk = ser.read(ser.in_waiting)
                
                # Protect against ESP32 Serial Bootlogs corrupting the binary 
                if not synced:
                    buffer += chunk
                    idx = buffer.find(MAGIC_NUMBER)
                    if idx != -1:
                        print("\n[+] PCAPNG Magic Number detected! Synchronized.")
                        synced = True
                        chunk = buffer[idx:] # Discard all ASCII bootlogs
                    else:
                        continue # Keep waiting
                
                f.write(chunk)
                f.flush()
                
                # Visual Indicator logic!
                print(".", end='', flush=True)

except KeyboardInterrupt:
    print("\n\n[+] Stream Terminated Safely.")
    print(f"[+] Output saved correctly to: {OUTPUT_FILE}")
    print("[+] Note: You can now run this directly through `hcxpcapngtool`!")
finally:
    ser.close()
