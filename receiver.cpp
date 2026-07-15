#include <iostream>
#include <vector>
#include <array>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

using namespace std;

const int PAYLOAD_SIZE = 160;
const int HISTORY_SIZE = 32768;

array<bool, HISTORY_SIZE> received_frames = {false};
uint16_t max_seq = 0;
bool max_seq_init = false;

long long current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int main() {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr{};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (::bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr)) < 0) {
        perror("bind 47002 failed");
        return 1;
    }

    int player_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player{};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    int nack_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in nack_relay{};
    nack_relay.sin_family = AF_INET;
    nack_relay.sin_port = htons(47003);
    nack_relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    uint8_t buf[2048];
    long long last_nack_time = 0;

    while (true) {
        ssize_t n = recvfrom(in_fd, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < 162) continue; 

        uint16_t header;
        memcpy(&header, buf, 2);
        header = ntohs(header);
        
        bool has_redundant = (header & 0x8000) != 0;
        uint16_t seq = header & 0x7FFF;
        
        if (!max_seq_init || seq > max_seq) {
            max_seq = seq;
            max_seq_init = true;
        }

        // Only process the payload if we haven't seen it yet
        if (!received_frames[seq]) {
            received_frames[seq] = true;
            uint8_t out_buf[164];
            uint32_t net_seq = htonl(seq);
            memcpy(out_buf, &net_seq, 4);
            memcpy(out_buf + 4, buf + 2, PAYLOAD_SIZE);
            sendto(player_fd, out_buf, 164, 0, (struct sockaddr *)&player, sizeof(player));
        }

        // Deal with the second payload if there is one
        if (has_redundant && n >= 324) {
            uint16_t red_seq;
            memcpy(&red_seq, buf + 162, 2);
            red_seq = ntohs(red_seq);
            
            if (!received_frames[red_seq]) {
                received_frames[red_seq] = true;
                uint8_t out_buf[164];
                uint32_t net_red_seq = htonl(red_seq);
                memcpy(out_buf, &net_red_seq, 4);
                memcpy(out_buf + 4, buf + 164, PAYLOAD_SIZE);
                sendto(player_fd, out_buf, 164, 0, (struct sockaddr *)&player, sizeof(player));
            }
        }

        long long now = current_time_ms();
        if (max_seq_init && (now - last_nack_time >= 20)) {
            vector<uint16_t> missing;
            
            // Look a bit behind the max sequence in case things just got shuffled on the wire
            int start_seq = (max_seq >= 100) ? (max_seq - 100) : 0;
            int end_seq = (max_seq >= 4) ? (max_seq - 4) : -1;
            
            for (int i = start_seq; i <= end_seq; i++) {
                if (!received_frames[i]) {
                    missing.push_back(htons(i));
                    if (missing.size() == 100) break;
                }
            }
            
            if (!missing.empty()) {
                sendto(nack_fd, missing.data(), missing.size() * 2, 0, (struct sockaddr *)&nack_relay, sizeof(nack_relay));
            }
            last_nack_time = now;
        }
    }
    return 0;
}
