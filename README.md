<div align="center">
<h1>Qyun Commu 青云通信</h1>
<div>一虹飞架连霄汉，万语穿云瞬息还</div>
<br>
</div>

这个工具为一实时消息广播系统，支持多频道通信架构。用户可以通过创建频道实现消息的分组广播，并通过订阅频道实时接收相关消息。系统具备低延迟、高并发的特性，适用于需要实时数据传输的场景，如在线协作、实时监控或即时通知等。通过简洁的 API 接口，用户可以轻松实现消息的发送与接收，构建高效的实时通信网络。

## 服务器端

### 主要功能

- **创建频道**：通过 HTTP GET 请求创建新的频道。
- **监听频道**：通过 WebSocket 连接监听指定频道中的消息。
- **发送消息**：通过 HTTP GET 请求向指定频道广播消息。

### 运行方法

1. 确保已安装 Python 3.8+ 和 FastAPI 框架。
2. 进入 `server` 目录，运行以下命令启动服务器：

   ```bash
   python ./run.py
   ```

   服务器默认运行在 `http://localhost:8000`。

### API 接口

- **创建频道**：
  - 请求方法：`GET`
  - 路径：`/channel/create/{name}`
  - 示例：`http://localhost:8000/channel/create/test`

- **监听频道**：
  - 请求方法：`WebSocket`
  - 路径：`/channel/listen/{name}`
  - 示例：`ws://localhost:8000/channel/listen/test`

- **发送消息**：
  - 请求方法：`GET`
  - 路径：`/channel/send/{name}/{message}`
  - 示例：`http://localhost:8000/channel/send/test/Hello`



好的！以下是关于 `client` 的详细说明，包括调用方法和每个函数的具体用途。

---

## Client 调用方法

### 编译和运行

1. **编译客户端代码**  
   确保已安装 `libcurl` 和 `libwebsockets` 库，然后使用以下命令测试客户端代码是否能正常编译：

   ```bash
   g++ -o test_latency.out test_latency.cpp -lcurl -lwebsockets -lpthread
   ```

2. **运行客户端**  
   编译完成后，运行以下命令启测试客户端：

   ```bash
   ./test_latency.out
   ```

   客户端将自动创建一个名为 `test` 的频道，发送 100 条消息到频道并计算平均延迟。

---

## Client 函数说明

### `create_channel`

**用途**
创建一个频道。

**参数**

- `host_name`：服务器地址，例如 `"localhost:8000"`。
- `name`：要创建的频道名称。

**返回值**

- `bool`：如果创建成功返回 `true`，否则返回 `false`。

**示例**

```cpp
bool success = create_channel("localhost:8000", "test");
if (success) {
    std::cout << "Channel created successfully!" << std::endl;
} else {
    std::cerr << "Failed to create channel." << std::endl;
}
```

---

### `send_message`

**用途**
通过 HTTP 请求向指定频道发送一条消息。

**参数**

- `host_name`：服务器地址，例如 `"localhost:8000"`。
- `channel`：目标频道名称。
- `message`：要发送的消息内容。

**返回值**

- `bool`：如果发送成功返回 `true`，否则返回 `false`。

**示例**

```cpp
bool success = send_message("localhost:8000", "test", "Hello, World!");
if (success) {
    std::cout << "Message sent successfully!" << std::endl;
} else {
    std::cerr << "Failed to send message." << std::endl;
}
```

---

### `listen_channel`

**用途**
通过 WebSocket 连接监听指定频道中的消息。

**参数**

- `host_name`：服务器地址，例如 `"localhost:8000"`。
- `channel_name`：要监听的频道名称。

**返回值**

- `int`：如果监听成功返回 `0`，否则返回 `-1`。

**示例**

```cpp
int result = listen_channel("localhost:8000", "test");
if (result == 0) {
    std::cout << "Listening to channel successfully!" << std::endl;
} else {
    std::cerr << "Failed to listen to channel." << std::endl;
}
```

---

### `websocket_callback`

**用途**
WebSocket 事件回调函数，用于处理 WebSocket 连接的各种事件（如连接建立、消息接收、连接关闭等）。

**参数**

- `wsi`：WebSocket 实例。
- `reason`：事件类型（如 `LWS_CALLBACK_CLIENT_ESTABLISHED` 表示连接建立）。
- `user`：用户数据。
- `in`：接收到的消息数据。
- `len`：消息长度。

**返回值**

- `int`：返回 `0` 表示成功处理事件。

**示例**

```cpp
int websocket_callback(struct lws* wsi, enum lws_callback_reasons reason,
                       void* user, void* in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            std::cout << "Connected to WebSocket server" << std::endl;
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE:
            std::cout << "Received: " << std::string((char*)in, len) << std::endl;
            break;
        case LWS_CALLBACK_CLIENT_CLOSED:
            std::cout << "Connection closed" << std::endl;
            break;
        default:
            break;
    }
    return 0;
}
```

---



## 注意事项

- 服务器端和客户端需要运行在同一台机器上，或者确保客户端能够访问服务器的 IP 地址和端口。
- 测试延迟时，确保服务器端已启动并正常运行。

## 许可证

本项目以自定义开源许可证形式开源。