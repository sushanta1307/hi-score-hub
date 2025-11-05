#include <cppkafka/cppkafka.h>
// #include <cppkafka/admin_client.h>       
#include <cppkafka/exceptions.h>
#include <librdkafka/rdkafka.h> 
#include <iostream>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <unistd.h>
#include "generated/score_update.pb.h"

constexpr int PORT = 7000;
const std::string BOOTSTRAP = "localhost:9092";
const std::string TOPIC     = "score_updates";

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    cppkafka::Configuration config = {
        { "bootstrap.servers", BOOTSTRAP },
        { "enable.idempotence", true },     // exactly-once per partition
        { "acks", "all" },                  // safest when idempotence = true
        { "max.in.flight", 5 },             // <=5 to avoid re-ordering
        { "retries", std::numeric_limits<int>::max() }
    };

    // Create a TCP socket
    int server = socket(AF_INET, SOCK_STREAM, 0);

    // Bind to any local address and the specified port
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    bind(server, (sockaddr*)&server_addr, sizeof(server_addr));

    // Start listening for incoming connections
    listen(server, 1);
    std::cout << "Gateway listening on port " << PORT << std::endl; 

    // Create a Kafka Topic
    // try {
    //     cppkafka::AdminClient admin(config);
    //     cppkafka::NewTopic nt(TOPIC, 3, 1);
    //     admin.create_topic(nt).get();           // blocks until done
    //     std::cout << "Topic \"" << TOPIC << "\" created.\n";
    // } catch (const cppkafka::TopicException& ex) {
    //     if (ex.get_error().value() == RD_KAFKA_ERR_TOPIC_ALREADY_EXISTS)
    //         std::cout << "Topic already exists – continue.\n";
    //     else {
    //         std::cerr << "Topic creation failed: " << ex.what() << '\n';
    //         return 1;
    //     }
    // }

    cppkafka::Producer producer(config);

    while(1) {
        // Wait for one client to connect
        int client = accept(server, nullptr, nullptr);
        std::cout << "Client connected" << std::endl;    
        
        std::string inbuf;
        char buffer[1024];
        ssize_t bytes_received;
        while ((bytes_received = recv(client, buffer, sizeof(buffer), 0)) > 0) {
            inbuf.append(buffer, buffer + bytes_received);
            
            // process newline-delimited JSON messages
            size_t pos;
            while ((pos = inbuf.find('\n')) != std::string::npos) {
                std::string line = inbuf.substr(0, pos);
                inbuf.erase(0, pos + 1);
                if (line.empty()) continue;

                try {
                    leaderboard::ScoreUpdate upd;
                    auto json = nlohmann::json::parse(line);

                    // validate leaderboard id ("lb"): must be a non-empty string and < 64 chars
                    if (!json.contains("lb") || !json["lb"].is_string()) {
                        std::cerr << "Invalid message: missing or non-string 'lb' field; skipping\n";
                        continue;
                    }
                    std::string lb = json["lb"].get<std::string>();
                    if (lb.empty() || lb.size() >= 64) {
                        std::cerr << "Invalid leaderboard id 'lb' (empty or >=64 chars): '" << lb << "'; skipping\n";
                        continue;
                    }
                    upd.set_leaderboard_id(lb);

                    
                    // validate player id ("player"): must be a non-empty string and < 128 chars
                    if (!json.contains("player") || !json["player"].is_string()) {
                        std::cerr << "Invalid message: missing or non-string 'player' field; skipping\n";
                        continue;
                    }
                    std::string player = json["player"].get<std::string>();
                    if (player.empty() || player.size() >= 128) {
                        std::cerr << "Invalid leaderboard id 'lb' (empty or >=64 chars): '" << lb << "'; skipping\n";
                        continue;
                    }
                    upd.set_player_id(player);
                    
                    // validate score: integer in [0, 2^63-1]
                    if (!json.contains("score") || !(json["score"].is_number_integer() || json["score"].is_number_unsigned())) {
                        std::cerr << "Invalid message: missing or non-integer 'score' field; skipping\n";
                        continue;
                    }

                    const uint64_t SCORE_MAX = static_cast<uint64_t>(LLONG_MAX); // 2^63-1
                    int64_t score_val = 0;
                    if (json["score"].is_number_unsigned()) {
                        uint64_t u = json["score"].get<uint64_t>();
                        if (u > SCORE_MAX) {
                            std::cerr << "Invalid score: too large (" << u << "); skipping\n";
                            continue;
                        }
                        score_val = static_cast<int64_t>(u);
                    } else {
                        // signed integer
                        int64_t s = json["score"].get<int64_t>();
                        if (s < 0) {
                            std::cerr << "Invalid score: negative (" << s << "); skipping\n";
                            continue;
                        }
                        score_val = s;
                    }
                    upd.set_score(score_val);

                    if (json.contains("ts_ms")) {
                        upd.set_timestamp_ms(json["ts_ms"].get<int64_t>());
                    } else {
                        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
                        ).count();
                        upd.set_timestamp_ms(ms);
                    }

                    // Debug print: original JSON and proto summary
                    std::cout << "Parsed JSON: " << line << std::endl;
                    std::string proto_out;
                    upd.SerializeToString(&proto_out); // or use toString helper if available
                    std::cout << "Proto size: " << proto_out.size() << " bytes\n";

                    std::string key = upd.player_id();
                    producer.produce(cppkafka::MessageBuilder(TOPIC).key(key).payload(proto_out));
                    producer.flush();                       // wait for broker ack

                    std::cout << "Message published to \"" << TOPIC << "\".\n";

                } catch (const std::exception& e) {
                    std::cerr << "Failed to parse JSON: " << e.what() << std::endl;
                }
            }
        }

        close(client);
    }
    
    // Clean up
    google::protobuf::ShutdownProtobufLibrary();
    close(server); 
    return 0;
}