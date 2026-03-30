#include <metavision/sdk/stream/camera.h>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>

// main loop
int main(int argc, char *argv[])
{
  float duration_seconds = 1.0f;

  for (int i = 1; i < argc; ++i)
  {
    if (std::strcmp(argv[i], "-t") == 0 || std::strcmp(argv[i], "--time") == 0)
    {
      if (i + 1 >= argc)
      {
        std::cerr << "Missing value for -t/--time option." << std::endl;
        return 1;
      }

      char *end_ptr = nullptr;
      duration_seconds = std::strtof(argv[++i], &end_ptr);
      if (end_ptr == argv[i] || *end_ptr != '\0' || duration_seconds <= 0.0f)
      {
        std::cerr << "Invalid duration: '" << argv[i] << "'. Please provide a positive float in seconds." << std::endl;
        return 1;
      }
    }
    else if (argv[i][0] == '-')
    {
      std::cerr << "Unknown option: '" << argv[i] << "'. Supported option: -t <seconds>." << std::endl;
      return 1;
    }
    else
    {
      std::cerr << "Unexpected positional argument: '" << argv[i] << "'. Use -t <seconds>." << std::endl;
      return 1;
    }
  }

  Metavision::Camera cam; // create the camera

  // open the first available camera with EVT3 encoder format
  Metavision::DeviceConfig device_config;
  device_config.set_format("EVT3");
  cam = Metavision::Camera::from_first_available(device_config);

  // record incoming events to RAW
  cam.start_recording("events.raw");

  // start the camera
  cam.start();

  const auto start_time = std::chrono::steady_clock::now();
  const auto max_duration = std::chrono::duration<float>(duration_seconds);

  // keep running while the camera is on or the recording is not finished
  while (cam.is_running())
  {
    const auto elapsed = std::chrono::steady_clock::now() - start_time;
    if (elapsed >= max_duration)
    {
      break;
    }

    // avoid a tight busy loop while waiting for the duration to elapse
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // the recording is finished, stop the camera.
  // Note: we will never get here with a live camera
  cam.stop();
  cam.stop_recording();
}