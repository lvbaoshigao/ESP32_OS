#!/usr/bin/env python3
"""Serial test harness for ESP32_OS.

Resets the board via RTS (EN), keeps DTR False (GPIO9 high = normal boot,
per how_to_read.MD pitfall #14), captures boot output, optionally sends a
sequence of shell commands and captures their replies.

Usage:
  serialtest.py [--port /dev/ttyACM0] [--baud 115200] [--boot-secs 2.0]
                [--cmd "wifisearch"] [--cmd "wifiinfo"] [--after 1.5]
"""
import sys, time, argparse
import serial

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='/dev/ttyACM0')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--boot-secs', type=float, default=2.0,
                    help='seconds to capture after reset before sending cmds')
    ap.add_argument('--cmd', action='append', default=[],
                    help='shell command to send (repeatable)')
    ap.add_argument('--after', type=float, default=1.5,
                    help='seconds to capture after each command')
    ap.add_argument('--no-reset', action='store_true')
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.1)
    ser.dsrdtr = False
    ser.rtscts = False

    if not args.no_reset:
        # RTS -> EN (reset). DTR must stay False (GPIO9 high = normal boot).
        ser.dtr = False
        ser.rts = True     # hold in reset
        time.sleep(0.1)
        ser.reset_input_buffer()
        ser.rts = False    # release -> run
        sys.stderr.write('[test] reset pulse sent\n')

    def drain(secs):
        end = time.time() + secs
        buf = bytearray()
        while time.time() < end:
            data = ser.read(4096)
            if data:
                buf += data
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
        return bytes(buf)

    drain(args.boot_secs)

    for cmd in args.cmd:
        sys.stderr.write(f'\n[test] >>> {cmd}\n')
        ser.write((cmd + '\r\n').encode())
        ser.flush()
        drain(args.after)

    ser.close()
    sys.stdout.buffer.write(b'\n')

if __name__ == '__main__':
    main()
