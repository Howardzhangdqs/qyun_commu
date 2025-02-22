import requests
from urllib.parse import quote
import websockets
import asyncio


def create_channel(host_name: str, name: str) -> bool:
    """创建频道"""
    try:
        encoded_name = quote(name)
        url = f"http://{host_name}/channel/create/{encoded_name}"
        response = requests.get(url)
        return response.status_code == 200
    except Exception as e:
        print(f"Error creating channel: {str(e)}")
        return False


def send_message(host_name: str, channel: str, message: str) -> bool:
    """发送消息"""
    try:
        encoded_channel = quote(channel)
        encoded_message = quote(message)
        url = f"http://{host_name}/channel/send/{encoded_channel}/{encoded_message}"
        response = requests.get(url)
        return response.status_code == 200
    except Exception as e:
        print(f"Error sending message: {str(e)}")
        return False


async def listen_channel(host_name: str, channel_name: str):
    """监听频道（异步函数）"""
    try:
        encoded_channel = quote(channel_name)
        uri = f"ws://{host_name}/channel/listen/{encoded_channel}"

        async with websockets.connect(uri) as websocket:
            print(f"Listening on channel: {channel_name}")
            while True:
                message = await websocket.recv()
                print(f"Received message: {message}")

    except Exception as e:
        print(f"WebSocket error: {str(e)}")


async def send_periodically(host_name: str, channel: str):
    """定时发送消息"""
    while True:
        await asyncio.sleep(1)
        # 在后台线程执行同步的requests调用
        loop = asyncio.get_running_loop()
        try:
            success = await loop.run_in_executor(
                None, send_message, host_name, channel, "Hello from Python"
            )
            if success:
                print("Message sent successfully")
            else:
                print("Failed to send message")
        except Exception as e:
            print(f"Error in scheduled send: {str(e)}")


async def main():
    """主异步函数"""
    server_host = "localhost:8000"
    channel_name = "python_test_channel"

    # 创建频道（在后台线程执行同步调用）
    loop = asyncio.get_running_loop()
    create_success = await loop.run_in_executor(
        None, create_channel, server_host, channel_name
    )

    # if not create_success:
    #     print("Failed to create channel. Exiting.")
    #     return

    print(f"Channel '{channel_name}' created successfully")

    # 同时启动监听和定时发送任务
    listener_task = asyncio.create_task(listen_channel(server_host, channel_name))
    sender_task = asyncio.create_task(send_periodically(server_host, channel_name))

    # 等待任务完成（实际会一直运行直到被中断）
    await asyncio.gather(listener_task, sender_task, return_exceptions=True)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nProgram terminated by user")
