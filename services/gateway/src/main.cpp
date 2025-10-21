#include <cppkafka/cppkafka.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "generated/score_update.pb.h"

constexpr int LISTEN_PORT = 7000;

int main() {
    // 1. Kafka producer
    cppkafka::Configuration cfg = {
        {"bootstrap.servers", "localhost:9092"},
        {"enable.idempotence", true},
        {"linger.ms", 5},
        {"acks", "all"}
    };
    cppkafka::Producer producer(cfg);
    const std::string topic = "score_updates";

    // 2. Simple TCP listener
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(LISTEN_PORT);
    bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    listen(server_fd, 16);

    std::cout << "Gateway listening on :" << LISTEN_PORT << "\n";

    while (true) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0) { perror("accept"); continue; }

        // expect each message as one line of JSON: {"lb":"global","player":"p1","score":123}
        char buffer[512];
        ssize_t n = read(client, buffer, sizeof(buffer)-1);
        if (n <= 0) { close(client); continue; }
        buffer[n] = '\0';

        // naive parse (replace with rapidjson later)
        std::string msg(buffer);
        auto lb_pos = msg.find("\"lb\":\"");
        auto player_pos = msg.find("\"player\":\"");
        auto score_pos = msg.find("\"score\":");

        if (lb_pos == std::string::npos || player_pos == std::string::npos || score_pos == std::string::npos) {
            std::cerr << "Malformed input: " << msg << "\n";
            close(client);
            continue;
        }

        std::string lb  = msg.substr(lb_pos + 6, msg.find("\"", lb_pos + 6) - (lb_pos + 6));
        std::string player = msg.substr(player_pos + 10, msg.find("\"", player_pos + 10) - (player_pos + 10));
        long score = std::stol(msg.substr(score_pos + 8));

        leaderboard::ScoreUpdate proto;
        proto.set_leaderboard_id(lb);
        proto.set_player_id(player);
        proto.set_score(score);
        proto.set_timestamp_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch()).count());

        std::string bytes;
        proto.SerializeToString(&bytes);

        cppkafka::MessageBuilder builder(topic);
        builder.key(player);
        builder.payload(bytes);
        producer.produce(builder);
        producer.flush();

        std::cout << "Published " << player << ":" << score << "\n";
        close(client);
    }
}