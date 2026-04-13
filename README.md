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

Output file: `events.raw` (EVT3 format)

### 4. Decode the output file

```bash
metavision_evt3_raw_file_decoder /path/to/input.raw /path/to/output.csv
```

If you get like "Unknown command: metavision_evt3_raw_file_decoder", you have to compile the code first.

[Compiling C++ code samples](https://docs.prophesee.ai/stable/samples/compilation/compilation.html#chapter-samples-cpp-compilation)

1. `$ cd ~`
2. `$ cp -r /usr/local/share/metavision/standalone_samples/metavision_evt3_raw_file_decoder ~/`
3. `$ mkdir build && cd build`
4. `$ cmake .. -DCMAKE_BUILD_TYPE=Release`
5. `$ cmake --build . --config Release`

Finally, you can move the exe file to bin directory.
`$ sudo cp ./metavision_evt3_raw_file_decoder /usr/local/bin/`


Other decoders are here. [PROPHESEE Encoder/Decoder samples](https://docs.prophesee.ai/stable/samples/standalone.html)

## Notes

This repository is developed with reference to PROPHESEE's SDK, official documentation, and related public resources.

Referenced resources:

- https://docs.prophesee.ai/stable/get_started/get_started_cpp.html
- https://github.com/prophesee-ai/openeb

***

## Extra: Automating the camera setup

You can also automate the camera setup on boot with the following steps.

### 1. Register `genx320` in the OS configuration file

Open the configuration file:

```bash
sudo vi /boot/firmware/config.txt
```

Append the following lines to the end of the file:

```text
...
[cm5]
dtoverlay=dwc2,dr_mode=host

[all]
dtoverlay=genx320,cam0
```

### 2. Create a startup script

Create a script in your home directory:

```bash
vi ~/startup_camera.sh
```

Paste the following content into the file:

```bash
#!/bin/bash

cd /home/your-username # Replace this with your actual user name.
./rp5_setup_v4l.sh
```

Then make it executable:

```bash
chmod +x ~/startup_camera.sh
```

### 3. Create a systemd service

Create the service file:

```bash
sudo vi /etc/systemd/system/camera_setup.service
```

Add the following content:

```ini
[Unit]
Description=Raspberry Pi 5 Camera Setup Service
After=multi-user.target

[Service]
Type=oneshot
ExecStart=/home/your-username/startup_camera.sh
User=root
Group=root

[Install]
WantedBy=multi-user.target
```

Be sure to replace `your-username` with your actual user name.

### 4. Enable the service

Reload systemd:

```bash
sudo systemctl daemon-reload
```

Enable the service at boot:

```bash
sudo systemctl enable camera_setup.service
```

### 5. Reboot

```bash
sudo reboot
```
