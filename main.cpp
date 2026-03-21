#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/base/events/event_cd.h>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

// this function will be associated to the camera callback
void save_events_to_csv(std::ofstream &csv_file, const Metavision::EventCD *begin, const Metavision::EventCD *end)
{
  // this loop allows us to get access to each event received in this callback
  for (const Metavision::EventCD *ev = begin; ev != end; ++ev)
  {
    csv_file << ev->t << "," << ev->x << "," << ev->y << "," << static_cast<int>(ev->p) << "\n";
  }
}

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
  std::ofstream csv_file("events.csv");

  if (!csv_file)
  {
    std::cerr << "Failed to open events.csv for writing." << std::endl;
    return 1;
  }

  csv_file << "timestamp,x,y,polarity\n";

  // open the first available camera
  cam = Metavision::Camera::from_first_available();

  // save incoming events to CSV
  cam.cd().add_callback([&csv_file](const Metavision::EventCD *begin, const Metavision::EventCD *end)
                        { save_events_to_csv(csv_file, begin, end); });

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
}