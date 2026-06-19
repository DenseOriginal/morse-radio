import serial
import serial.tools.list_ports
import threading
import sys
import time

BAUD_RATE = 115200
MY_ID = "PC"

# Playback speed configuration for the remote (in milliseconds)
FAST_DIT_MS = 60
FAST_LETTER_MS = 150

def find_bridge_port():
    ports = serial.tools.list_ports.comports()
    keywords = ["CP210", "Silicon Labs", "CH340", "USB to UART", "FTDI"]
    candidates = []

    for port, desc, hwid in ports:
        if any(keyword.lower() in desc.lower() or keyword.lower() in hwid.lower() for keyword in keywords):
            candidates.append(port)

    if not candidates:
        print("[!] No compatible serial ports found.")
        sys.exit(1)

    print("Probing candidate ports for the Bridge...")
    for port in candidates:
        try:
            ser = serial.Serial(port, BAUD_RATE, timeout=0.5)
            ser.setDTR(False)
            ser.setRTS(False)
            time.sleep(1.5) 
            ser.write(b"PING\n")
            ser.flush()

            start_time = time.time()
            while time.time() - start_time < 1.0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if "BRIDGE_ACK" in line or "BRIDGE_LOG" in line:
                    ser.close()
                    print(f"[✓] Bridge confirmed on {port}")
                    return port
            ser.close()
        except Exception:
            pass

    print("\n[!] Could not verify the Bridge on any port.")
    sys.exit(1)

def receive_from_bridge(ser):
    while True:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                if line.startswith("BRIDGE_LOG:"):
                    print(f"\n[🔧 {line}]")
                elif ":" in line:
                    parts = line.split(":")
                    sender_id = parts[0]
                    # Handle both ID:MSG and ID:DIT:LETTER:MSG responses in the console
                    payload = parts[-1] 
                    if sender_id != MY_ID:
                        print(payload, end='', flush=True)
                else:
                    print(line, end='', flush=True)
        except serial.SerialException:
            print("\n[!] Connection to bridge lost.")
            break
        except Exception:
            pass

def main():
    serial_port = find_bridge_port()

    try:
        ser = serial.Serial(serial_port, BAUD_RATE, timeout=0.1)
        ser.setDTR(False)
        ser.setRTS(False)
    except Exception as e:
        print(f"[!] Could not open port {serial_port}: {e}")
        sys.exit(1)

    print(f"\nConnected to LoRa Bridge on {serial_port}.")
    print("Type a message and press Enter to send at any time.\n" + "-"*40)

    listener_thread = threading.Thread(target=receive_from_bridge, args=(ser,), daemon=True)
    listener_thread.start()

    try:
        while True:
            msg = input()
            if msg:
                # Format: ID:DIT:LETTER:MSG
                tx_payload = f"{MY_ID}:{FAST_DIT_MS}:{FAST_LETTER_MS}:{msg}\n"
                ser.write(tx_payload.encode('utf-8'))
                ser.flush()
    except KeyboardInterrupt:
        print("\nExiting...")
        ser.close()

if __name__ == '__main__':
    main()