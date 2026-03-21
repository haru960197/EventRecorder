#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/base/events/event_cd.h>

// this function will be associated to the camera callback
void count_events(const Metavision::EventCD *begin, const Metavision::EventCD *end)
{
  int counter = 0;

  // this loop allows us to get access to each event received in this callback
  for (const Metavision::EventCD *ev = begin; ev != end; ++ev)
  {
    ++counter; // count each event

    // print each event
    std::cout << "Event received: coordinates (" << ev->x << ", " << ev->y << "), t: " << ev->t
              << ", polarity: " << ev->p << std::endl;
  }

  // report
  std::cout << "There were " << counter << " events in this callback" << std::endl;
}

// main loop
int main(int argc, char *argv[])
{
  Metavision::Camera cam; // create the camera

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

  // to analyze the events, we add a callback that will be called periodically to give access to the latest events
  cam.cd().add_callback(count_events);

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