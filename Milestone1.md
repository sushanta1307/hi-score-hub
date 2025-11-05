Milestone 1 — Event Ingestion Gateway (learn-by-building)

Goal
- A small TCP gateway service that accepts score updates from clients and publishes them to Kafka.

Outcomes
- One running process (gateway)
- One Kafka topic (score_updates)
- A smoke test proving end-to-end delivery

1) Functional Requirements

R1. Transport in
- Listen on TCP port (default 7000).
- Each client sends one line per message (LF-terminated).

R2. Input format (two options — pick one)
- R2a. JSON (easiest to start, then you can switch to Protobuf)
  Payload per line:
  {
    "lb": "global",
    "player": "alice",
    "score": 9000,
    "ts_ms": 1730000000000   // optional, if missing gateway fills server time
  }
- R2b. Protobuf (binary) with 4-byte length prefix (advanced)

Start with R2a. We’ll evolve to R2b later.

R3. Validation
- lb: non-empty string, <= 64 chars
- player: non-empty string, <= 128 chars
- score: int64 in [0, 2^63-1]
- ts_ms: optional int64, if absent set to server-now (epoch ms)

Reject malformed lines; do not crash.

R4. Publish to Kafka
- Topic: score_updates
- Key: player (string)
- Value: Protobuf bytes of ScoreUpdate (recommended) OR JSON bytes (temp)
- Exactly-once publishing (idempotent producer) ON by default.

R5. Topic bootstrap
- If topic doesn’t exist, create it (12 partitions, RF=1 for local dev).
- Config: compression.type=producer, min.insync.replicas=1

R6. Concurrency
- Accept multiple TCP clients concurrently.
- Each connection is handled independently.

R7. Backpressure
- If Kafka is temporarily unavailable, buffer a small number of messages in-memory (e.g., 10k), then apply backpressure: stop reading the socket until buffer drains.

R8. Health & metrics (basic)
- /health (optional HTTP on 7001 or log-only): reports “OK” if producer is constructed and last Kafka error is older than 30s.
- Log counters: accepted, published, rejected, retried.

R9. Configuration by env vars (no flags required)
- GATEWAY_LISTEN_ADDR: default 0.0.0.0
- GATEWAY_LISTEN_PORT: default 7000
- KAFKA_BOOTSTRAP_SERVERS: default localhost:9092 (host) or kafka:29092 (container)
- KAFKA_TOPIC: default score_updates
- KAFKA_PARTITIONS: default 12 (used only on create)
- ENABLE_IDEMPOTENCE: default true
- BUFFER_MAX_MSGS: default 10000
- LOG_LEVEL: info|debug

2) Non-Functional Requirements

NFR1. Reliability
- Idempotence enabled (enable.idempotence=true, acks=all, linger.ms=5).
- Retries infinite (librdkafka default) with a reasonable message.timeout.ms (e.g., 60000).

NFR2. Performance
- Handle at least 5k msgs/sec on a laptop for small messages.

NFR3. Robustness
- No crash on malformed input or Kafka hiccups.
- Graceful shutdown: drain in-flight queue (with max wait 5s).

NFR4. Security (dev-mode)
- Bind to localhost by default in dev if you prefer; for now 0.0.0.0 is acceptable.
- No secrets in code. Kafka auth not required for local.

NFR5. Observability
- Structured logs (timestamp, level, component, message).
- Periodic metrics log line (every 10s): accepted=N, published=N, rejected=N, buffered=N.


3) Message Contract (Protobuf) — recommended value encoding

file: proto/score_update.proto
syntax = "proto3";
package leaderboard;

message ScoreUpdate {
  string leaderboard_id = 1;
  string player_id      = 2;
  int64  score          = 3;
  int64  timestamp_ms   = 4;
}

Notes
- Gateway parses JSON into this structure, then serializes Protobuf bytes for Kafka value.
- Key = player_id.
- Later components (Streams app) will parse Protobuf.


4) Kafka Producer Configuration (baseline)

bootstrap.servers = ${KAFKA_BOOTSTRAP_SERVERS}
enable.idempotence = true
acks = all
linger.ms = 5
compression.type = lz4
message.timeout.ms = 60000
retries = INT_MAX (default)
delivery.timeout.ms = 120000

5) Topic Creation Policy

On startup:
- Describe topic score_updates.
- If it does not exist: create with Kafka Admin API (or librdkafka Admin API):
  partitions = ${KAFKA_PARTITIONS} (default 12)
  replication-factor = 1
  config: cleanup.policy=delete, compression.type=producer, min.insync.replicas=1

Log: “Topic ready: score_updates”.

6) Error Handling

E1. Malformed JSON
- Log: level=warn, reason=malformed-input, include first 200 chars.
- Do not close the server. Continue accepting next lines.

E2. Validation failure
- Log: level=warn, reason=validation-failed, field=<name>.
- Drop the message.

E3. Kafka produce error
- librdkafka will retry; track a “last_error_ts”.
- If produce() returns a hard error (e.g., message too large), count rejected and log.

E4. Backpressure
- If queue > BUFFER_MAX_MSGS:
  - Stop reading from the socket (pause).
  - When queue drains below 80% of max, resume reading.

E5. Shutdown
- On SIGINT/SIGTERM:
  - Stop accepting new connections.
  - Flush producer up to 5s, then exit 0.


  7) Logging Format (examples)

[INFO] gateway.start listen=0.0.0.0:7000 kafka=localhost:9092 topic=score_updates
[INFO] topic.ready name=score_updates partitions=12 created=false
[INFO] publish.ok key=alice size=42
[WARN] input.malformed peer=127.0.0.1:53124 bytes="...truncated..."
[WARN] validation.failed field=score reason=negative
[ERROR] publish.error key=alice code=MSG_TIMED_OUT
[METRICS] ts=1730000000 accepted=12500 published=12490 rejected=10 buffered=30

8) Directory & Build Skeleton (suggested)

services/gateway/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── kafka_producer.hpp / .cpp
│   ├── tcp_server.hpp / .cpp
│   ├── json_parser.hpp / .cpp
│   ├── metrics.hpp / .cpp
│   └── generated/score_update.pb.{h,cc}  # from protoc
└── include/ (optional)

Build notes
- Link: librdkafka, cppkafka (or call librdkafka directly), protobuf.
- Unit-test json_parser and validation (optional for M1).

9) Minimal Algorithm (JSON → Protobuf → Kafka)

For each accepted TCP line:
- Parse JSON (lb, player, score, ts_ms?).
- Validate fields.
- Build ScoreUpdate:
  su.leaderboard_id = lb
  su.player_id = player
  su.score = score
  su.timestamp_ms = ts_ms or now()
- Serialize su to bytes.
- Kafka produce(topic=score_updates, key=player, value=bytes).


10) Step-by-Step Implementation Plan

S1. Bootstrapping
- Implement TCP listener (blocking or epoll/ASIO).
- Log “gateway.start …”.

S2. JSON parse + validation
- Implement a simple parser (basic find/substring or a small JSON lib if you already have one).
- Validate per R3.

S3. Protobuf serialization
- Compile proto → generated files.
- Convert parsed fields → Protobuf → bytes.

S4. Kafka producer
- Construct producer with idempotence.
- Implement topic ensure (Admin API).
- Implement publish(key, value) with delivery callback that logs success/failure.

S5. Backpressure & buffers
- Use a bounded queue between TCP reader and Kafka producer thread (e.g., SPSC/MPSC).
- Pause socket read when size exceeds BUFFER_MAX_MSGS.

S6. Metrics & logs
- Add counters and periodic metrics line.

S7. Shutdown
- Catch SIGINT/SIGTERM.
- Stop accept(), drain queue, flush producer, exit.

S8. Manual smoke (see below).

11) Manual Smoke (small commands)

# A. Run gateway (terminal A)
services/gateway/build/gateway

# B. Verify Kafka is up (terminal B)
CID_KAFKA=$(podman compose ps -q kafka)
podman exec -it "$CID_KAFKA" /usr/local/kafka_2.13-3.6.1/bin/kafka-broker-api-versions.sh \
  --bootstrap-server localhost:9092

# C. Send 5 test messages (terminal C)
echo '{"lb":"global","player":"alice","score":10}'  | nc localhost 7000
echo '{"lb":"global","player":"alice","score":50}'  | nc localhost 7000
echo '{"lb":"global","player":"bob","score":7}'     | nc localhost 7000
echo '{"lb":"global","player":"bob","score":99}'    | nc localhost 7000
echo '{"lb":"global","player":"carol","score":5}'   | nc localhost 7000

# D. List topics (terminal B)
podman exec -it "$CID_KAFKA" /usr/local/kafka_2.13-3.6.1/bin/kafka-topics.sh \
  --bootstrap-server localhost:9092 --list | grep score_updates || echo "topic not found"

# E. Check end offsets (terminal B)
podman exec -it "$CID_KAFKA" /usr/local/kafka_2.13-3.6.1/bin/kafka-run-class.sh \
  kafka.tools.GetOffsetShell --broker-list localhost:9092 --topic score_updates --time -1

# F. Optional peek (raw protobuf will look binary)
podman exec -it "$CID_KAFKA" /usr/local/kafka_2.13-3.6.1/bin/kafka-console-consumer.sh \
  --bootstrap-server localhost:9092 --topic score_updates \
  --from-beginning --max-messages 5 --property print.key=true --property key.separator=" | "

  12) Acceptance Criteria

AC1. Gateway starts and logs topic readiness.
AC2. Well-formed test lines → published (publish.ok logs increase).
AC3. Malformed lines → rejected (warn logs, no crash).
AC4. End offsets for score_updates increase after sending test messages.
AC5. Gateway handles concurrent connections (open 2 shells, send lines simultaneously).
AC6. Graceful shutdown drains remaining in-flight messages (no lost message if Kafka is up).

Stretch (after M1 passes)
- Add a tiny HTTP /health endpoint.
- Switch value encoding from JSON to Protobuf for Kafka (if you started with JSON).
- Add mTLS/Kerberos support in config stubs (for future production).
