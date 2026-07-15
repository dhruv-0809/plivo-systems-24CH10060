#include <iostream>
#include <vector>
#include <array>
#include <deque>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std;

const int PAYLOAD_SIZE = 160;
const int HISTORY_SIZE = 32768;

// Keep track of old frames so we can re-send them
array<array<uint8_t, PAYLOAD_SIZE>, HISTORY_SIZE> history;
array<bool, HISTORY_SIZE> has_history = {false};

deque<uint16_t> nack_queue;

void push_nack(uint16_t seq) {
    // Check if it's already in the queue so we don't spam it
    for (uint16_t existing : nack_queue) {
        if (existing == seq) return;
    }
    // Limit size just to be safe
    if (nack_queue.size() < 1000) {
        nack_queue.push_back(seq);
    }
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr{};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (::bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr)) < 0) {
        perror("bind 47010 failed");
        return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay{};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    int nack_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in nack_addr{};
    nack_addr.sin_family = AF_INET;
    nack_addr.sin_port = htons(47004);
    nack_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (::bind(nack_fd, (struct sockaddr *)&nack_addr, sizeof(nack_addr)) < 0) {
        perror("bind 47004 failed");
        return 1;
    }
    set_nonblocking(nack_fd);

    uint8_t buf[2048];
    int tokens = 0;
    
    // We get about 320 bytes per frame max to stay under 2x limit.
    // 315 leaves a bit of breathing room for NACK messages.
    const int TOKENS_PER_FRAME = 315;
    const int MAX_TOKENS = 1000;
    
    const int BASE_PACKET_SIZE = 162;
    const int EXT_PACKET_SIZE = 324;

    while (true) {
        ssize_t n = recvfrom(in_fd, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < 164) continue;

        // Try to read any pending NACKs from receiver
        uint8_t nack_buf[1024];
        while (true) {
            ssize_t nn = recvfrom(nack_fd, nack_buf, sizeof(nack_buf), 0, nullptr, nullptr);
            if (nn <= 0) break;
            
            int num_nacks = nn / 2;
            for (int i = 0; i < num_nacks; i++) {
                uint16_t missing_seq;
                memcpy(&missing_seq, nack_buf + i * 2, 2);
                missing_seq = ntohs(missing_seq);
                push_nack(missing_seq);
            }
        }

        uint32_t full_seq;
        memcpy(&full_seq, buf, 4);
        full_seq = ntohl(full_seq);
        uint16_t seq = full_seq & 0x7FFF;
        
        // Save payload for later
        memcpy(history[seq].data(), buf + 4, PAYLOAD_SIZE);
        has_history[seq] = true;

        tokens += TOKENS_PER_FRAME;
        if (tokens > MAX_TOKENS) {
            tokens = MAX_TOKENS;
        }

        bool send_red = false;
        uint16_t red_seq = 0;

        // Decide if we have enough tokens to pack an extra frame
        if (tokens >= EXT_PACKET_SIZE) {
            if (!nack_queue.empty()) {
                // Someone asked for a missing frame, send it!
                red_seq = nack_queue.front();
                nack_queue.pop_front();
                if (has_history[red_seq]) {
                    send_red = true;
                }
            } else {
                // Otherwise just send a frame from a few steps ago just in case
                int offset = 3;
                if (seq >= offset) {
                    red_seq = seq - offset;
                    if (has_history[red_seq]) {
                        send_red = true;
                    }
                }
            }
        }

        // Pack the data to send
        vector<uint8_t> out_buf(512);
        int out_len = 0;
        
        uint16_t header = seq;
        if (send_red) {
            header |= 0x8000; // Flip highest bit so receiver knows there's a 2nd frame
            tokens -= EXT_PACKET_SIZE;
        } else {
            tokens -= BASE_PACKET_SIZE;
        }
        
        uint16_t net_header = htons(header);
        memcpy(out_buf.data(), &net_header, 2);
        out_len += 2;
        memcpy(out_buf.data() + out_len, history[seq].data(), PAYLOAD_SIZE);
        out_len += PAYLOAD_SIZE;
        
        if (send_red) {
            uint16_t net_red_seq = htons(red_seq);
            memcpy(out_buf.data() + out_len, &net_red_seq, 2);
            out_len += 2;
            memcpy(out_buf.data() + out_len, history[red_seq].data(), PAYLOAD_SIZE);
            out_len += PAYLOAD_SIZE;
        }

        sendto(out_fd, out_buf.data(), out_len, 0, (struct sockaddr *)&relay, sizeof(relay));
    }
    return 0;
}
