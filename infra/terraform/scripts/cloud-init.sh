#!/bin/bash
set -e

# Install dependencies
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get -y install git podman podman-compose

# Clone repo & checkout desired branch
git clone --depth 1 --branch ${git_branch} ${git_repo_url} /opt/leaderboard

# Launch stack
cd /opt/leaderboard
podman-compose -f docker-compose.yml up -d