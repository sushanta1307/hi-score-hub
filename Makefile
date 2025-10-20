.PHONY: up down tail java-build cpp-build all test

up:            ## Start local stack with Podman
	@podman-compose up -d --build

down:          ## Stop & remove containers + volumes
	@podman-compose down -v

tail:          ## Follow logs
	@podman-compose logs -f

java-build:    ## Build all Java modules
# 	@mvn -q -pl services/rank-aggregator, services/batch-job -am clean package
	@mvn -q -pl services/batch-job -am clean package

cpp-build:     ## Build all C++ modules
	@cmake -S services/gateway -B services/gateway/build -DCMAKE_BUILD_TYPE=Release
	@cmake --build services/gateway/build --parallel
	@cmake -S services/ranking-service -B services/ranking-service/build -DCMAKE_BUILD_TYPE=Release
	@cmake --build services/ranking-service/build --parallel

all: java-build cpp-build

test:          ## Place-holder for unit + integration tests
	@mvn test