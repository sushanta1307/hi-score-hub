#!/usr/bin/env bash
set -euo pipefail

# Absolute paths in this image
KBIN="/usr/local/kafka_2.13-3.6.1/bin"
S1="$KBIN/kafka-server-start.sh"
S2="$KBIN/kafka-run-class.sh"
STORAGE="$KBIN/kafka-storage.sh"

# Sanity checks
if [[ ! -x "$STORAGE" ]]; then
  echo "[kafka] ERROR: $STORAGE missing or not executable" >&2
  exit 1
fi

# Ensure storage dirs exist and are writable by runtime user
mkdir -p /u01/app/kafka/kafka_data /u01/app/kafka/logs
# Your image runs as user 'kafka', so make dirs owned by that user
# (if it runs as numeric UID, change to chown -R "$(id -u)":"$(id -g)")
chown -R kafka:kafka /u01/app/kafka/kafka_data /u01/app/kafka/logs || true

# KRaft single-node config (broker + controller in one process)
cat > /tmp/server.properties <<'EOF'
node.id=1
process.roles=broker,controller

listeners=PLAINTEXT://0.0.0.0:9092,INTERNAL://0.0.0.0:29092,CONTROLLER://0.0.0.0:9093
advertised.listeners=PLAINTEXT://localhost:9092,INTERNAL://kafka:29092
listener.security.protocol.map=PLAINTEXT:PLAINTEXT,INTERNAL:PLAINTEXT,CONTROLLER:PLAINTEXT

controller.listener.names=CONTROLLER
controller.quorum.voters=1@kafka:9093

log.dirs=/u01/app/kafka/kafka_data

num.partitions=1
offsets.topic.replication.factor=1
transaction.state.log.replication.factor=1
transaction.state.log.min.isr=1
auto.create.topics.enable=true
group.initial.rebalance.delay.ms=0
EOF

# One-time format if not yet formatted
if [[ ! -f /u01/app/kafka/kafka_data/meta.properties ]]; then
  echo "[kafka] formatting KRaft storage..."
  CID="$("$STORAGE" random-uuid | tr -d '\r\n ' )"
  if [[ -z "$CID" ]]; then
    echo "[kafka] ERROR: generated empty cluster id" >&2
    exit 1
  fi
  "$STORAGE" format -t "$CID" -c /tmp/server.properties
  echo "[kafka] formatted with cluster-id=$CID"
fi

# Launch Kafka
if [[ -x "$S1" ]]; then
  exec "$S1" /tmp/server.properties
elif [[ -x "$S2" ]]; then
  exec "$S2" kafka.Kafka /tmp/server.properties
else
  echo "[kafka] ERROR: neither $S1 nor $S2 is executable" >&2
  exit 1
fi
