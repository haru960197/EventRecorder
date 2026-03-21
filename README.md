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
```

### 3. Run

```bash
# default: record for 1.0 second
./event_recorder

# record for 1.8 seconds
./event_recorder -t 1.8

# same as above
./event_recorder --time 1.8
```
