/*
 * Copyright 2020-present Ksyun
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "pch.h"

#include "engine.h"

#include "udp_server.h"
#include "ws_server.h"

#include "shared_mem_info.h"

void Engine::Run(tcp::endpoint ws_endpoint,
                 udp::endpoint udp_endpoint,
                 std::string audio_codec,
                 uint64_t audio_bitrate,
                 bool enable_nvenc,
                 uint64_t video_bitrate,
                 int video_gop,
                 std::string video_preset,
                 uint32_t video_quality) {
  try {
    encoder_.Init(std::move(audio_codec), audio_bitrate, enable_nvenc,
                  video_bitrate, video_gop, std::move(video_preset),
                  video_quality);

    if (0 != ws_endpoint.port()) {
      ws_server_ = std::make_shared<WsServer>(*this, ws_endpoint);
      std::cout << "WebSocket server on: " << ws_endpoint << '\n';
      ws_server_->Run();
    }
    udp_server_ = std::make_shared<UdpServer>(*this, udp_endpoint);
    std::cout << "UDP Server on: " << udp_endpoint << '\n';
    udp_server_->Run();
  } catch (std::exception& e) {
    std::cerr << e.what() << '\n';
    return;
  }

  running_ = true;
  auto io_thread = std::thread(&Engine::Loop, this);
  Loop();
  io_thread.join();
  running_ = false;
}

void Engine::Loop() noexcept {
  for (;;) {
    try {
      ioc_.run();
      break;
    } catch (std::exception& e) {
      std::cerr << e.what() << '\n';
    }
  }
}

void Engine::Stop() {
  if (!running_) {
    return;
  }
  try {
    ioc_.stop();
    running_ = false;
  } catch (std::exception& e) {
    std::cerr << e.what() << '\n';
  }
}

void Engine::EncoderRun() {
  encoder_.Run();
}

void Engine::EncoderStop() {
  encoder_.Stop();
}

int Engine::OnWritePacket(void* opaque,
                          uint8_t* buffer,
                          int buffer_size) noexcept {
#if _DEBUG
  // std::cout << __func__ << ": " << buffer_size << "\n";
#endif
  return Engine::GetInstance().WritePacket(opaque, 0, buffer, buffer_size);
}

int Engine::WritePacket(void* opaque,
                        uint32_t timestamp,
                        uint8_t* body,
                        int body_size) noexcept {
  auto ei = static_cast<EncoderInterface*>(opaque);
  std::string buffer;
  buffer.resize(sizeof(NetPacketHeader) + body_size);
  NetPacketHeader* header = reinterpret_cast<NetPacketHeader*>(buffer.data());
  header->type = static_cast<uint32_t>(ei->GetType());
  header->ts = htonl(timestamp) >> 8;
  header->size = htonl(body_size);
  memcpy(buffer.data() + sizeof(NetPacketHeader), body, body_size);
  ws_server_->Send(std::move(buffer));
  return 0;
}