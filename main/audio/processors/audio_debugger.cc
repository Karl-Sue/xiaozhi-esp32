#include "audio_debugger.h"
#include "sdkconfig.h"

#if CONFIG_USE_AUDIO_DEBUGGER
#include <esp_log.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <mbedtls/base64.h>

namespace {
std::string EncodeBase64(const uint8_t* data, size_t len) {
    size_t output_len = 0;
    int ret = mbedtls_base64_encode(nullptr, 0, &output_len, data, len);
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || output_len == 0) {
        return "";
    }

    std::string encoded(output_len, '\0');
    ret = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded.data()), output_len, &output_len, data, len);
    if (ret != 0) {
        return "";
    }

    encoded.resize(output_len);
    return encoded;
}
}  // namespace
#endif

#define TAG "AudioDebugger"


AudioDebugger::AudioDebugger() {
#if CONFIG_USE_AUDIO_DEBUGGER
    udp_sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sockfd_ >= 0) {
        // 解析配置的服务器地址 "IP:PORT"
        std::string server_addr = CONFIG_AUDIO_DEBUG_UDP_SERVER;
        size_t colon_pos = server_addr.find(':');
        
        if (colon_pos != std::string::npos) {
            std::string ip = server_addr.substr(0, colon_pos);
            int port = std::stoi(server_addr.substr(colon_pos + 1));
            
            memset(&udp_server_addr_, 0, sizeof(udp_server_addr_));
            udp_server_addr_.sin_family = AF_INET;
            udp_server_addr_.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &udp_server_addr_.sin_addr);
            
            ESP_LOGI(TAG, "Initialized server address: %s", CONFIG_AUDIO_DEBUG_UDP_SERVER);
        } else {
            ESP_LOGW(TAG, "Invalid server address: %s, should be IP:PORT", CONFIG_AUDIO_DEBUG_UDP_SERVER);
            close(udp_sockfd_);
            udp_sockfd_ = -1;
        }
    } else {
        ESP_LOGW(TAG, "Failed to create UDP socket: %d", errno);
    }
#endif
}

AudioDebugger::~AudioDebugger() {
#if CONFIG_USE_AUDIO_DEBUGGER
    if (udp_sockfd_ >= 0) {
        close(udp_sockfd_);
        ESP_LOGI(TAG, "Closed UDP socket");
    }
#endif
}

void AudioDebugger::Feed(const std::vector<int16_t>& data) {
#if CONFIG_USE_AUDIO_DEBUGGER
    const uint8_t* pcm_bytes = reinterpret_cast<const uint8_t*>(data.data());
    const size_t pcm_len = data.size() * sizeof(int16_t);
    std::string encoded = EncodeBase64(pcm_bytes, pcm_len);
    if (!encoded.empty()) {
        constexpr size_t kChunkSize = 512;
        for (size_t i = 0; i < encoded.size(); i += kChunkSize) {
            size_t chunk_len = std::min(kChunkSize, encoded.size() - i);
            ESP_LOGI(TAG, "MIC_PCM_BASE64: %.*s", static_cast<int>(chunk_len), encoded.c_str() + i);
        }
    } else {
        ESP_LOGW(TAG, "Failed to encode PCM to base64");
    }

    if (udp_sockfd_ >= 0) {
        ssize_t sent = sendto(udp_sockfd_, data.data(), data.size() * sizeof(int16_t), 0,
                             (struct sockaddr*)&udp_server_addr_, sizeof(udp_server_addr_));
        if (sent < 0) {
            ESP_LOGW(TAG, "Failed to send audio data to %s: %d", CONFIG_AUDIO_DEBUG_UDP_SERVER, errno);
        } else {
            ESP_LOGD(TAG, "Sent %d bytes audio data to %s", sent, CONFIG_AUDIO_DEBUG_UDP_SERVER);
        }
    }
#endif
}

 
