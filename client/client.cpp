#include <curl/curl.h>
#include <libwebsockets.h>

#include <iostream>
#include <string>

// libcurl 的响应回调函数，用于处理 HTTP 响应数据
size_t write_callback(void* contents, size_t size, size_t nmemb,
                      std::string* s) {
  size_t newLength = size * nmemb;
  s->append((char*)contents, newLength);  // 将响应数据追加到字符串中
  return newLength;                       // 返回实际处理的数据长度
}

// 创建频道的函数
bool create_channel(const std::string& host_name, const std::string& name) {
  CURL* curl = curl_easy_init();  // 初始化 CURL
  if (!curl) {
    std::cerr << "初始化 CURL 失败" << std::endl;
    return false;
  }
  char* escaped_name = curl_easy_escape(
      curl, name.c_str(), name.length());  // 对频道名称进行 URL 编码
  std::string url =
      "http://" + host_name + "/channel/create/" + std::string(escaped_name);
  curl_free(escaped_name);  // 释放编码后的字符串
  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());  // 设置请求 URL
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                   write_callback);  // 设置回调函数
  curl_easy_setopt(curl, CURLOPT_WRITEDATA,
                   &response);             // 设置回调函数的数据接收对象
  CURLcode res = curl_easy_perform(curl);  // 执行 HTTP 请求
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE,
                    &http_code);                         // 获取 HTTP 响应码
  bool success = (res == CURLE_OK && http_code == 200);  // 判断请求是否成功
  if (!success) {
    std::cerr << "创建频道失败。HTTP 状态码: " << http_code
              << ", 响应内容: " << response << std::endl;
  }
  curl_easy_cleanup(curl);  // 清理 CURL 资源
  return success;
}

// 发送消息的函数
bool send_message(const std::string& host_name, const std::string& channel,
                  const std::string& message) {
  CURL* curl = curl_easy_init();  // 初始化 CURL
  if (!curl) {
    std::cerr << "初始化 CURL 失败" << std::endl;
    return false;
  }
  char* escaped_channel = curl_easy_escape(
      curl, channel.c_str(), channel.length());  // 对频道名称进行 URL 编码
  char* escaped_message = curl_easy_escape(
      curl, message.c_str(), message.length());  // 对消息内容进行 URL 编码
  std::string url = "http://" + host_name + "/channel/send/" +
                    std::string(escaped_channel) + "/" +
                    std::string(escaped_message);
  curl_free(escaped_channel);  // 释放编码后的字符串
  curl_free(escaped_message);  // 释放编码后的字符串
  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());  // 设置请求 URL
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                   write_callback);  // 设置回调函数
  curl_easy_setopt(curl, CURLOPT_WRITEDATA,
                   &response);             // 设置回调函数的数据接收对象
  CURLcode res = curl_easy_perform(curl);  // 执行 HTTP 请求
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE,
                    &http_code);                         // 获取 HTTP 响应码
  bool success = (res == CURLE_OK && http_code == 200);  // 判断请求是否成功
  if (!success) {
    std::cerr << "发送消息失败。HTTP 状态码: " << http_code
              << ", 响应内容: " << response << std::endl;
  }
  curl_easy_cleanup(curl);  // 清理 CURL 资源
  return success;
}

// 用于保持 WebSocket 连接的全局变量
static struct lws_context* g_ws_context = nullptr;  // WebSocket 上下文
static struct lws* g_ws_wsi = nullptr;              // WebSocket 连接实例
static std::string g_pending_message;               // 待发送的消息
static bool g_message_sent = false;                 // 消息是否已发送标志
static std::string g_current_channel;               // 当前连接的频道
static std::string g_current_host;                  // 当前连接的主机

// WebSocket 写消息回调函数
static int ws_send_callback(struct lws* wsi, enum lws_callback_reasons reason,
                            void* user, void* in, size_t len) {
  switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:  // 连接建立时触发
      lws_callback_on_writable(wsi);       // 标记连接可写
      break;
    case LWS_CALLBACK_CLIENT_WRITEABLE: {  // 连接可写时触发
      if (!g_pending_message.empty()) {
        unsigned char
            buf[LWS_PRE + g_pending_message.length() + 1];  // 准备缓冲区
        memcpy(&buf[LWS_PRE], g_pending_message.c_str(),
               g_pending_message.length());  // 填充消息内容
        int result = lws_write(wsi, &buf[LWS_PRE], g_pending_message.length(),
                               LWS_WRITE_TEXT);  // 发送消息
        if (result < 0) {
          std::cerr << "WebSocket 消息发送失败" << std::endl;
          return -1;
        }
        g_message_sent = true;      // 标记消息已发送
        g_pending_message.clear();  // 清空待发送消息
      }
      break;
    }
    case LWS_CALLBACK_CLOSED:  // 连接关闭时触发
      g_ws_wsi = nullptr;      // 清理连接实例
      break;
    default:
      break;
  }
  return 0;
}

// WebSocket 协议配置（仅用于发送）
static struct lws_protocols send_protocols[] = {
    {"sender-protocol", ws_send_callback, 0, 0}, {NULL, NULL, 0, 0}};

// 通过 WebSocket 发送消息
bool send_message_ws(const std::string& host_name, const std::string& channel,
                     const std::string& message) {
  // 如果需要新建连接或切换频道
  if (!g_ws_context || g_current_host != host_name ||
      g_current_channel != channel) {
    if (g_ws_context) {
      lws_context_destroy(g_ws_context);  // 销毁旧的 WebSocket 上下文
      g_ws_context = nullptr;
      g_ws_wsi = nullptr;
    }
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;  // 不监听端口
    info.protocols = send_protocols;     // 设置协议
    info.gid = -1;
    info.uid = -1;
    g_ws_context = lws_create_context(&info);  // 创建新的 WebSocket 上下文
    if (!g_ws_context) {
      std::cerr << "创建 WebSocket 上下文失败" << std::endl;
      return false;
    }
    size_t colon_pos = host_name.find(':');  // 分离主机名和端口号
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
    g_ws_wsi = lws_client_connect_via_info(&ccinfo);  // 建立 WebSocket 连接
    if (!g_ws_wsi) {
      std::cerr << "连接 WebSocket 发送通道失败" << std::endl;
      lws_context_destroy(g_ws_context);
      g_ws_context = nullptr;
      return false;
    }
    g_current_host = host_name;   // 保存当前主机信息
    g_current_channel = channel;  // 保存当前频道信息
  }
  g_pending_message = message;  // 设置待发送消息
  g_message_sent = false;       // 重置消息发送标志
  if (g_ws_wsi) {
    lws_callback_on_writable(g_ws_wsi);  // 标记连接可写
    int timeout = 100;                   // 设置超时时间（10 秒）
    while (!g_message_sent && timeout > 0) {
      lws_service(g_ws_context, 100);  // 处理 WebSocket 事件
      timeout--;
    }
    return g_message_sent;  // 返回消息是否发送成功
  }
  return false;
}

// 定义 WebSocket 回调函数类型
typedef int (*websocket_callback_fn)(struct lws* wsi,
                                     enum lws_callback_reasons reason,
                                     void* user, void* in, size_t len);

// 全局指针用于存储注册的回调函数
static websocket_callback_fn g_websocket_callback = nullptr;

// 注册自定义回调函数
void regist_websocket_callback(websocket_callback_fn callback) {
  g_websocket_callback = callback;
}

// 默认的 WebSocket 回调实现
int default_websocket_callback(struct lws* wsi,
                               enum lws_callback_reasons reason, void* user,
                               void* in, size_t len) {
  switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:  // 连接建立时触发
      std::cout << "已连接到 WebSocket 服务器" << std::endl;
      break;
    case LWS_CALLBACK_CLIENT_RECEIVE: {  // 接收到消息时触发
      std::cout << "接收到消息: " << std::string((char*)in, len) << std::endl;
      break;
    }
    case LWS_CALLBACK_CLIENT_CLOSED:  // 连接关闭时触发
      std::cout << "连接已关闭" << std::endl;
      break;
    default:
      break;
  }
  return 0;
}

// WebSocket 回调包装器，用于分发回调
int websocket_callback_wrapper(struct lws* wsi,
                               enum lws_callback_reasons reason, void* user,
                               void* in, size_t len) {
  if (g_websocket_callback) {
    return g_websocket_callback(wsi, reason, user, in, len);  // 使用注册的回调
  } else {
    return default_websocket_callback(wsi, reason, user, in,
                                      len);  // 使用默认回调
  }
}

// WebSocket 协议配置
static struct lws_protocols protocols[] = {
    {"channel-protocol", websocket_callback_wrapper, 0, 0}, {NULL, NULL, 0, 0}};

// 监听频道的函数
int listen_channel(const std::string& host_name,
                   const std::string& channel_name) {
  struct lws_context_creation_info info;
  struct lws_context* context;
  struct lws_client_connect_info ccinfo = {0};
  memset(&info, 0, sizeof(info));
  info.port = CONTEXT_PORT_NO_LISTEN;  // 不监听端口
  info.protocols = protocols;          // 设置协议
  info.gid = -1;
  info.uid = -1;
  context = lws_create_context(&info);  // 创建 WebSocket 上下文
  if (!context) {
    std::cerr << "创建 WebSocket 上下文失败" << std::endl;
    return -1;
  }
  size_t colon_pos = host_name.find(':');  // 分离主机名和端口号
  std::string host = host_name.substr(0, colon_pos);
  int port = std::stoi(host_name.substr(colon_pos + 1));
  std::cout << "[Qyun Commu Client] 正在连接 WebSocket: " << host << ":" << port
            << std::endl;
  std::string path = "/channel/listen/" + channel_name;
  std::string origin = "http://" + host_name;
  ccinfo.context = context;
  ccinfo.address = host.c_str();
  ccinfo.port = port;
  ccinfo.path = path.c_str();
  ccinfo.host = host.c_str();
  ccinfo.origin = origin.c_str();
  ccinfo.protocol = protocols[0].name;
  struct lws* wsi =
      lws_client_connect_via_info(&ccinfo);  // 建立 WebSocket 连接
  if (!wsi) {
    std::cerr << "[Qyun Commu Client] 连接 WebSocket 失败" << std::endl;
    lws_context_destroy(context);
    return -1;
  }
  std::cout << "[Qyun Commu Client] 正在监听频道: " << channel_name
            << std::endl;
  while (true) {
    lws_service(context, 0);  // 持续处理 WebSocket 事件
  }
  lws_context_destroy(context);  // 销毁上下文
  return 0;
}