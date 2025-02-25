#include <libwebsockets.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "./client.cpp"

// 用于存储每次测试的时间差
std::vector<long long> latency_results;

// 全局变量，用于存储发送消息的时间
std::chrono::high_resolution_clock::time_point send_time;

// WebSocket回调函数
int websocket_callback(struct lws* wsi, enum lws_callback_reasons reason,
                       void* user, void* in, size_t len) {
  switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      std::cout << "Connected to WebSocket server" << std::endl;
      break;
    case LWS_CALLBACK_CLIENT_RECEIVE: {
      auto end_time = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                          end_time - send_time)
                          .count();
      latency_results.push_back(duration);
      std::cout << "Received: " << std::string((char*)in, len) << " in "
                << duration << " microseconds" << std::endl;
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

int main(int argc, char* argv[]) {
  std::string host_name = "localhost:8000";
  std::string channel_name = "test";

  regist_websocket_callback(websocket_callback);

  create_channel(host_name, channel_name);

  std::thread listener(listen_channel, host_name, channel_name);
  listener.detach();

  int test_count = 0;
  while (test_count < 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 30));

    send_message_ws(host_name, channel_name, "Hello, World!");
    // 记录发送消息的时间
    send_time = std::chrono::high_resolution_clock::now();
    test_count++;
  }

  // 等待所有测试完成
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // 计算平均延迟
  long long total_latency = 0;
  for (auto latency : latency_results) {
    total_latency += latency;
  }
  long long average_latency = total_latency / latency_results.size();
  std::cout << "Average latency: " << average_latency << " microseconds"
            << std::endl;

  return 0;
}