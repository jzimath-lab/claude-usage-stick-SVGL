import sys, time, serial

import glob
port = (glob.glob("/dev/cu.usbmodem*") or ["/dev/cu.usbmodem1101"])[0]
dur = float(sys.argv[1]) if len(sys.argv) > 1 else 8.0

s = serial.Serial(port, 115200, timeout=0.2)
# Reset ESP32-S3 (USB-Serial/JTAG): pulsa DTR/RTS
s.setDTR(False); s.setRTS(True); time.sleep(0.15)
s.setRTS(False); time.sleep(0.05)
s.reset_input_buffer()

end = time.time() + dur
buf = b""
while time.time() < end:
    data = s.read(4096)
    if data:
        buf += data
s.close()
sys.stdout.write(buf.decode("utf-8", errors="replace"))
