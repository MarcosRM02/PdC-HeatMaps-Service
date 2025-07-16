<div align="center">
    <img width="300" height="163" alt="MAmI_logo" src="https://github.com/user-attachments/assets/871eedfe-0812-4ac1-9679-3f49ba5aea1f" />
    <img width="310" height="168" alt="images" src="https://github.com/user-attachments/assets/0185155b-463e-4ba2-bd47-085d5ebd1536" />
</div>

This repository is one component of the MVP‑Gait toolkit, a collection of interoperable services including an Application Program Interface, Front‑End web Service, and an Animation Generator Microservice, developed to support the [SSITH](https://mamilab.eu/ssith-project/) project and advance research in human gait studies.

# **MVP-Gait Animation Generator Microservice**

This microservice, written in C++17, consumes Redis tasks, fetches sensor data via HTTP from the PdC Visualization API, processes point clouds into heat map images using OpenCV & Eigen, and stores or forwards results to downstream systems (PostgreSQL, file storage).

## 🚀 Features

- Redis queue consumer for task orchestration
    
- HTTP client to fetch CSV/JSON data from PdC Visualization API
    
- Numerical operations with Eigen
    
- JSON parsing with RapidJSON
    
- Dockerfile for containerized build & run
    
- Animated heat map sequences with Center of Pressure overlay
	
- Synchronization of generated animations with recorded video sessions

## 📋 Requirements

- C++17-compatible compiler (e.g., `g++`)
    
- [OpenCV](https://opencv.org/) development headers
    
- [Eigen3](https://eigen.tuxfamily.org/) library
    
- [RapidJSON](https://rapidjson.org/) headers
    
- `libhiredis` for Redis
    
- `libcurl` for HTTP
    
- `libpq` for PostgreSQL
    
- Running Redis server
    
- Accessible PdC Visualization API endpoint
    

## 🔧 Installation

1. Clone the repository:
    
    ```bash
    git clone <repository-url>
    cd PdC-HeatMaps-Service-main
    ```
    
2. Install dependencies on Debian/Ubuntu:
    
    ```bash
    sudo apt update && sudo apt install -y \
      g++ libopencv-dev libeigen3-dev \
      libcurl4-openssl-dev libpq-dev \
      libhiredis-dev
    ```
    
3. (Optional) Build `hiredis` from source if your distro lacks `libhiredis-dev`:
    
    ```bash
    git clone https://github.com/redis/hiredis.git && cd hiredis && make && sudo make install
    ```
    

## 🛠️ Building & Running

### 🔨 Manual Build

```bash
mkdir -p bin

g++ -std=c++17 \
  -I./include -I/usr/include/eigen3 -I/usr/include/opencv4 \
  -o bin/hm_Service hm_Service.cpp \
  -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_videoio \
  -lhiredis -lcurl -lpq
```

### 🚀 Run Service

```bash
./bin/hm_Service
```

### ⚙️ Using `run.sh`

```bash
chmod +x run.sh
./run.sh
```

The script waits for Redis & API, then compiles and launches the service.

## 🐳 Docker Usage

Build and run the containerized service:

```bash
docker build -t pdc-heatmaps-service .
```

```bash
docker run --network your_network \
  -e REDIS_HOST=<REDIS_HOST> \
  -e API_HOST=<API_HOST> \
  pdc-heatmaps-service
```

## 📁 Project Structure

```
PdC-HeatMaps-Service-main/
├── hm_Service.cpp           # Main C++ implementation
├── include/                 # Header-only libs (csv.hpp, rapidjson)
├── in/                      # Sample JSON input files
├── bin/                     # Compiled binary output
├── run.sh                   # Build & run helper script
├── Dockerfile               # Docker build definition
```

## 📑 Scripts & Helpers

- `run.sh`: orchestrates dependency readiness, build, and execution
