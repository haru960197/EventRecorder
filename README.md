## Get Started

### 1. Loading the sensor driver and formats

```bash
cd ~
sudo dtoverlay genx320,cam0
./rp5_setup_v4l.sh
```

### 2. Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

./event_recorder
```
