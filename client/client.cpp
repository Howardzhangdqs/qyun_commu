#include <curl/curl.h>
#include <libwebsockets.h>

#include <iostream>
#include <string>

// libcurl响应回调函数
size_t write_callback(void* contents, size_t size, size_t nmemb,
                      std::string* s) {
  size_t newLength = size * nmemb;
  s->append((char*)contents, newLength);
  return newLength;
}

// 创建频道函数
bool create_channel(const std::string& host_name, const std::string& name) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    std::cerr << "Failed to initialize CURL" << std::endl;
    return false;
  }

  char* escaped_name = curl_easy_escape(curl, name.c_str(), name.length());
  std::string url =
      "http://" + host_name + "/channel/create/" + std::string(escaped_name);
  curl_free(escaped_name);

  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  bool success = (res == CURLE_OK && http_code == 200);
  if (!success) {
    std::cerr << "Failed to create channel. HTTP code: " << http_code
              << ", Response: " << response << std::endl;
  }

  curl_easy_cleanup(curl);
  return success;
}

// 发送消息函数
bool send_message(const std::string& host_name, const std::string& channel,
                  const std::string& message) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    std::cerr << "Failed to initialize CURL" << std::endl;
    return false;
  }

  char* escaped_channel =
      curl_easy_escape(curl, channel.c_str(), channel.length());
  char* escaped_message =
      curl_easy_escape(curl, message.c_str(), message.length());
  std::string url = "http://" + std::string(host_name) + "/channel/send/" +
                    std::string(escaped_channel) + "/" +
                    std::string(escaped_message);
  curl_free(escaped_channel);
  curl_free(escaped_message);

  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  bool success = (res == CURLE_OK && http_code == 200);
  if (!success) {
    std::cerr << "Failed to send message. HTTP code: " << http_code
              << ", Response: " << response << std::endl;
  }

  curl_easy_cleanup(curl);
  return success;
}

// 用于保持WebSocket连接的全局变量
static struct lws_context* g_ws_context = nullptr;
static struct lws* g_ws_wsi = nullptr;
static std::string g_pending_message;
static bool g_message_sent = false;
static std::string g_current_channel;
static std::string g_current_host;

// WebSocket写消息回调
static int ws_send_callback(struct lws* wsi, enum lws_callback_reasons reason,
                            void* user, void* in, size_t len) {
  switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      // 连接建立后发送消息
      lws_callback_on_writable(wsi);
      break;
    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      if (!g_pending_message.empty()) {
        // 准备发送的消息需要添加LWS协议头部
        unsigned char buf[LWS_PRE + g_pending_message.length() + 1];
        memcpy(&buf[LWS_PRE], g_pending_message.c_str(),
               g_pending_message.length());

        // 发送消息
        int result = lws_write(wsi, &buf[LWS_PRE], g_pending_message.length(),
                               LWS_WRITE_TEXT);
        if (result < 0) {
          std::cerr << "WebSocket write failed" << std::endl;
          return -1;
        }
        g_message_sent = true;
        g_pending_message.clear();
      }
      break;
    }
    case LWS_CALLBACK_CLOSED:
      // 连接关闭，清理资源
      g_ws_wsi = nullptr;
      break;
    default:
      break;
  }
  return 0;
}

// WebSocket协议配置(仅用于发送)
static struct lws_protocols send_protocols[] = {
    {"sender-protocol", ws_send_callback, 0, 0}, {NULL, NULL, 0, 0}};

// 通过WebSocket发送消息
bool send_message_ws(const std::string& host_name, const std::string& channel,
                     const std::string& message) {
  // 如果需要新建连接或切换通道
  if (!g_ws_context || g_current_host != host_name ||
      g_current_channel != channel) {
    // 清理旧连接
    if (g_ws_context) {
      lws_context_destroy(g_ws_context);
      g_ws_context = nullptr;
      g_ws_wsi = nullptr;
    }

    // 创建新的WebSocket连接
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = send_protocols;
    info.gid = -1;
    info.uid = -1;

    g_ws_context = lws_create_context(&info);
    if (!g_ws_context) {
      std::cerr << "Failed to create WebSocket context" << std::endl;
      return false;
    }

    // 分离主机名和端口
    size_t colon_pos = host_name.find(':');
    std::string host = host_name.substr(0, colon_pos);
    int port = std::stoi(host_name.substr(colon_pos + 1));

    std::string path = "/channel/send/" + channel;
    std::string origin = "http://" + host_name;

    struct lws_client_connect_info ccinfo = {0};
    ccinfo.context = g_ws_context;
    ccinfo.address = host.c_str();
    ccinfo.port = port;
    ccinfo.path = path.c_str();
    ccinfo.host = host.c_str();
    ccinfo.origin = origin.c_str();
    ccinfo.protocol = send_protocols[0].name;

    g_ws_wsi = lws_client_connect_via_info(&ccinfo);
    if (!g_ws_wsi) {
      std::cerr << "Failed to connect to WebSocket for sending" << std::endl;
      lws_context_destroy(g_ws_context);
      g_ws_context = nullptr;
      return false;
    }

    // 保存当前连接信息
    g_current_host = host_name;
    g_current_channel = channel;
  }

  // 设置要发送的消息
  g_pending_message = message;
  g_message_sent = false;

  // 标记连接可写并处理事件
  if (g_ws_wsi) {
    lws_callback_on_writable(g_ws_wsi);

    // 尝试处理WebSocket事件，直到消息发送或超时
    int timeout = 100;  // 10秒超时
    while (!g_message_sent && timeout > 0) {
      lws_service(g_ws_context, 100);  // 100ms服务间隔
      timeout--;
    }

    return g_message_sent;
  }

  return false;
}

// Define a type for the WebSocket callback function
typedef int (*websocket_callback_fn)(struct lws* wsi,
                                     enum lws_callback_reasons reason,
                                     void* user, void* in, size_t len);

// Global pointer to store the registered callback
static websocket_callback_fn g_websocket_callback = nullptr;

// Function to register a custom callback
void regist_websocket_callback(websocket_callback_fn callback) {
  g_websocket_callback = callback;
}

// Default WebSocket callback implementation
int default_websocket_callback(struct lws* wsi,
                               enum lws_callback_reasons reason, void* user,
                               void* in, size_t len) {
  switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      std::cout << "Connected to WebSocket server" << std::endl;
      break;
    case LWS_CALLBACK_CLIENT_RECEIVE: {
      std::cout << "Received: " << std::string((char*)in, len) << std::endl;
      break;
    }
    case LWS_CALLBACK_CLIENT_CLOSED:
      std::cout << "Connection closed" << std::endl;
      break;
    default:
      break;
  }
  return 0;
}

// Dispatcher callback used by libwebsockets
int websocket_callback_wrapper(struct lws* wsi,
                               enum lws_callback_reasons reason, void* user,
                               void* in, size_t len) {
  // If a custom callback is registered, use it; otherwise use the default
  if (g_websocket_callback) {
    return g_websocket_callback(wsi, reason, user, in, len);
  } else {
    return default_websocket_callback(wsi, reason, user, in, len);
  }
}

// WebSocket协议配置
static struct lws_protocols protocols[] = {
    {"channel-protocol", websocket_callback_wrapper, 0, 0}, {NULL, NULL, 0, 0}};

// 监听频道函数
int listen_channel(const std::string& host_name,
                   const std::string& channel_name) {
  struct lws_context_creation_info info;
  struct lws_context* context;
  struct lws_client_connect_info ccinfo = {0};

  memset(&info, 0, sizeof(info));
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = protocols;
  info.gid = -1;
  info.uid = -1;

  context = lws_create_context(&info);
  if (!context) {
    std::cerr << "Failed to create WebSocket context" << std::endl;
    return -1;
  }

  // Split host_name into host and port
  size_t colon_pos = host_name.find(':');
  std::string host = host_name.substr(0, colon_pos);
  int port = std::stoi(host_name.substr(colon_pos + 1));

  std::cout << "[Qyun Commu Client] Connecting to WebSocket: " << host << ":"
            << port << std::endl;

  std::string path = "/channel/listen/" + channel_name;
  std::string origin = "http://" + host_name;

  ccinfo.context = context;
  ccinfo.address = host.c_str();
  ccinfo.port = port;
  ccinfo.path = path.c_str();
  ccinfo.host = host.c_str();
  ccinfo.origin = origin.c_str();
  ccinfo.protocol = protocols[0].name;

  struct lws* wsi = lws_client_connect_via_info(&ccinfo);
  if (!wsi) {
    std::cerr << "[Qyun Commu Client] Failed to connect to WebSocket"
              << std::endl;
    lws_context_destroy(context);
    return -1;
  }

  std::cout << "[Qyun Commu Client] Listening on channel: " << channel_name
            << std::endl;
  while (true) {
    lws_service(context, 0);
  }

  lws_context_destroy(context);
  return 0;
}