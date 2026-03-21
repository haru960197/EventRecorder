#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/base/events/event_cd.h>
#include <fstream>
#include <iostream>

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
  Metavision::Camera cam; // create the camera
  std::ofstream csv_file("events.csv");

  if (!csv_file)
  {
    std::cerr << "Failed to open events.csv for writing." << std::endl;
    return 1;
  }

  csv_file << "timestamp,x,y,polarity\n";

  if (argc >= 2)
  {
    // if we passed a file path, open it
    cam = Metavision::Camera::from_file(argv[1]);
  }
  else
  {
    // open the first available camera
    cam = Metavision::Camera::from_first_available();
  }

  // save incoming events to CSV
  cam.cd().add_callback([&csv_file](const Metavision::EventCD *begin, const Metavision::EventCD *end)
                        { save_events_to_csv(csv_file, begin, end); });

  // start the camera
  cam.start();

  // keep running while the camera is on or the recording is not finished
  while (cam.is_running())
  {
  }

  // the recording is finished, stop the camera.
  // Note: we will never get here with a live camera
  cam.stop();
}