# Global Leaderboard

## Requirements

Goal- 
Ingest millions of score updates, show near-real-time rank queries.

Pipeline
• Game Gateway (C++): UDP/TCP listener, converts to Protobuf, produces to Kafka topic score_updates.
• Rank Aggregator (Java): Kafka Streams KTable keyed by (leaderboard_id, player_id) → keeps highest score.
• Cassandra
– score_history (player_id, ts) for audit / cheating detection.
– leaderboard_partition (lb_id, shard_id) → Redis-like sorted set encoded as Cassandra collection for hot shard hand-off.
• MySQL
– players(profile), friends lists, tournaments metadata.
• Ranking Service (C++ gRPC)
– Reads current shard from Cassandra, merges friend list from MySQL, returns top N.
• Nightly Batch (Spark on Java) rebuilds canonical leaderboard into MySQL table leaderboard_snapshot to correct drift.

Extras
• Hot-shard detection via consumer lag metrics → Kafka topic shard_split_request.

A real-time, horizontally scalable leaderboard service built with  
• Apache Kafka – event backbone  
• Cassandra – score history & hot-shard storage  
• MySQL – player metadata and tournament configs  
• Java / C++ micro-services – gRPC internal APIs, REST external endpoints

## Milestones
| Tag | Description | Status |
|-----|-------------|--------|
| v0.1 | Repo bootstrap, infra stack | DONE
| v0.2 | Event Ingestion Gateway (C++) | TODO
| v0.3 | Rank Aggregator (Java Kafka Streams) | TODO
| v0.4 | gRPC Ranking Service (C++) | TODO
| v1.0 | End-to-end demo with dashboard | TODO

## Local Dev (quick start)
```bash
git clone git@github.com:sushanta1307/hi-score-hub.git
cd hi-score-hub

## Local prerequisites (tested on Ubuntu 22.04 LTS)

# Core build & runtime
sudo apt update
sudo apt install -y \
  build-essential cmake clang-format \
  openjdk-17-jdk maven git curl

# Podman & podman-compose
sudo apt install -y podman
pip install --user podman-compose         # ~/.local/bin must be on $PATH

# Kafka CLI tooling (optional, good for debugging)
curl -L https://archive.apache.org/dist/kafka/3.6.1/kafka_2.13-3.6.1.tgz \
  | tar xz
echo 'export PATH=$PATH:$HOME/kafka_2.13-3.6.1/bin' >> ~/.bash_profile
source ~/.bash_profile

brew install podman podman-compose openjdk@17 cmake protobuf

```

### Start local Env
```bash
# Start the stack

# Select any base directory on the host
export ODO_DATA_DIR=$HOME/odo-data          # or another absolute path

# Create the hierarchy used by the image
mkdir -p $ODO_DATA_DIR/zk/{data,logs,conf}

# Make it writable for UID 1000 (the uid the image uses)
sudo chown -R 1000:1000 $ODO_DATA_DIR

# from repo root
podman-compose -f podman-compose.yml up -d        # or just 'podman-compose up -d'

# Verify Containers
podman ps
podman-compose logs -f          # follow logs

# Access UIs & ports
• Kafka-UI → http://localhost:8080
• Schema Registry → http://localhost:8081
• MySQL CLI → mysql -h 127.0.0.1 -P 3306 -u root -p
• Cassandra CQLSH → podman exec -it cassandra cqlsh

# Stop and clean up
podman-compose down -v          # removes volumes as well

# Deploy Terraform
cd infra/terraform
terraform init
terraform plan  -var-file=myenv.tfvars
terraform apply -var-file=myenv.tfvars
```


# Smoke Test
```sh 
# 0 Create the podman machine with more cpu and memory (6 cpu 8192 Memeory)

# 1. compile & package everything
make all

# 2. start infra
make up

# 3. open Kafka UI
open http://localhost:8080    # macOS
xdg-open http://localhost:8080  # Linux

# 4. run batch job once (will exit quickly because no data yet)
mvn -q -pl services/batch-job exec:java

# 5. teardown
make down
```

