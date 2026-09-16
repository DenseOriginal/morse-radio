import serial
import threading
import sys
import time

# Configuration
PORT = '/dev/ttyUSB0'  # Update to match your device's serial port
BAUD_RATE = 115200

def read_from_device(ser):
    """Continuously reads incoming LoRa messages forwarded by Bridge Mode."""
    try:
        while True:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting).decode('ascii', errors='ignore')
                if data:
                    print(f"\n[RX] {data}\n> ", end="", flush=True)
            else:
                # Yield CPU time to prevent 100% core utilization
                time.sleep(0.01)
    except Exception as e:
        pass

def main():
    try:
        ser = serial.Serial(PORT, BAUD_RATE, timeout=1)
        print(f"Connected to {PORT} at {BAUD_RATE} baud.")
        
        # Send activation string to force device into Bridge Mode
        ser.write(b'BRG_ON')
        print("Sent Bridge Mode activation string.")
        
        print("Type a device ID (1-7), * for all devices, a space, and a message (Ctrl+C to exit).\n")

        rx_thread = threading.Thread(target=read_from_device, args=(ser,), daemon=True)
        rx_thread.start()

        while True:
            try:
                line = input("> ")
                parts = line.split(' ', 1)
                if len(parts) == 2 and (parts[0] in '1234567' or parts[0] == '*') and parts[1]:
                    command = f"BRG_SEND:{parts[0]}:{parts[1]}"
                    ser.write(command.encode('ascii', errors='ignore'))
                else:
                    print("Use: <device ID 1-7 or *> <message>")
            except EOFError:
                break

    except serial.SerialException as e:
        print(f"Error: Could not open port {PORT}. {e}")
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        if 'ser' in locals() and ser.is_open:
            try:
                ser.write(b'BRG_OFF')
                time.sleep(0.1) # Allow time for the device to receive the command
            except:
                pass
            ser.close()

if __name__ == '__main__':
    main()