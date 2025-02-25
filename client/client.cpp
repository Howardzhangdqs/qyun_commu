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

extern int websocket_callback(struct lws* wsi, enum lws_callback_reasons reason,
                              void* user, void* in, size_t len);

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