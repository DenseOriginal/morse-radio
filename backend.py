import asyncio
import websockets

async def receive_messages(websocket):
    """Listens for incoming characters and prints them immediately."""
    try:
        async for message in websocket:
            # Print without newline to form words dynamically
            print(f"{message}", end='', flush=True)
    except websockets.exceptions.ConnectionClosed:
        print("\nESP32 Device disconnected.")

async def send_messages(websocket):
    """Waits for user input and sends it to the device."""
    try:
        while True:
            # Run blocking input() in a separate thread
            response = await asyncio.to_thread(input, "")
            if response:
                await websocket.send(response)
                print(f"\n[Sent: {response}]")
    except websockets.exceptions.ConnectionClosed:
        pass # Handled by receive_messages

async def handle_client(websocket):
    print("ESP32 Device connected. Type a message and press Enter to send at any time.\n---")
    
    # Run both tasks concurrently
    recv_task = asyncio.create_task(receive_messages(websocket))
    send_task = asyncio.create_task(send_messages(websocket))
    
    # Wait until either task finishes (connection closed)
    done, pending = await asyncio.wait(
        [recv_task, send_task],
        return_when=asyncio.FIRST_COMPLETED,
    )
    
    # Cancel the remaining pending tasks
    for task in pending:
        task.cancel()

async def main():
    async with websockets.serve(handle_client, "0.0.0.0", 8765):
        print("WebSocket server started on ws://0.0.0.0:8765")
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())