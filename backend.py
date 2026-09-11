import serial
import threading
import sys

# Configuration
PORT = '/dev/ttyUSB1'  # Update to match your device's serial port
BAUD_RATE = 115200

def read_from_device(ser):
    """Continuously reads incoming LoRa messages forwarded by Bridge Mode."""
    try:
        while True:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting).decode('ascii', errors='ignore')
                if data:
                    print(f"\n[RX] {data}\n> ", end="", flush=True)
    except Exception as e:
        pass

def main():
    try:
        # Connect to the Heltec V3
        ser = serial.Serial(PORT, BAUD_RATE, timeout=1)
        print(f"Connected to {PORT} at {BAUD_RATE} baud.")
        print("Ensure device is in 'BRG' (Bridge Mode) by holding the button during boot.")
        print("Type a message and press Enter to send (Ctrl+C to exit).\n")

        # Start background thread to listen for incoming messages
        rx_thread = threading.Thread(target=read_from_device, args=(ser,), daemon=True)
        rx_thread.start()

        # Main loop to write data
        while True:
            try:
                msg = input("> ")
                if msg:
                    # Device expects printable ASCII characters (32-126)
                    ser.write(msg.encode('ascii', errors='ignore'))
            except EOFError:
                break

    except serial.SerialException as e:
        print(f"Error: Could not open port {PORT}. {e}")
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()

if __name__ == '__main__':
    main()