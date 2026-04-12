#include <metavision/sdk/core/algorithms/periodic_frame_generation_algorithm.h>
#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/ui/utils/event_loop.h>
#include <metavision/sdk/ui/utils/window.h>

// we need to choose an accumulation time and a frame rate (here of 20ms and 50 fps)
const std::uint32_t ACC       = 20000;
const double FPS              = 50;

// main loop
int main(int argc, char *argv[]) {
    Metavision::Camera cam;       // create the camera

    if (argc >= 2) {
        // if we passed a file path, open it
        cam = Metavision::Camera::from_file(argv[1]);
    } else {
        // open "events.raw" by default
        cam = Metavision::Camera::from_file("events.raw");
    }

    // to visualize the events, we will need to build frames and render them.
    // building frame will be done with a frame generator that will accumulate the events over time.
    // we need to provide it the camera resolution that we can retrieve from the camera instance
    int camera_width  = cam.geometry().get_width();
    int camera_height = cam.geometry().get_height();

    // now we can create our frame generator using previous variables
    auto frame_gen = Metavision::PeriodicFrameGenerationAlgorithm(camera_width, camera_height, ACC, FPS);

    // we add the callback that will pass the events to the frame generator
    cam.cd().add_callback([&](const Metavision::EventCD *begin, const Metavision::EventCD *end) {
        frame_gen.process_events(begin, end);
    });

    // to render the frames, we create a window using the Window class of the UI module
    Metavision::Window window("Metavision SDK Get Started", camera_width, camera_height,
                              Metavision::BaseWindow::RenderMode::BGR);

    // we set a callback on the windows to close it when the Escape or Q key is pressed
    // Escape または Q キーが押されたときにウィンドウを閉じるためのコールバックを設定
    window.set_keyboard_callback(
        [&window](Metavision::UIKeyEvent key, int scancode, Metavision::UIAction action, int mods) {
            if (action == Metavision::UIAction::RELEASE &&
                (key == Metavision::UIKeyEvent::KEY_ESCAPE || key == Metavision::UIKeyEvent::KEY_Q)) {
                window.set_close_flag();
            }
        });

    // we set a callback on the frame generator so that it calls the window object to display the generated frames
    frame_gen.set_output_callback([&](Metavision::timestamp, cv::Mat &frame) { window.show(frame); });

    // start the camera
    cam.start();

    // keep running until the camera is off, the recording is finished or the escape key was pressed
    while (cam.is_running() && !window.should_close()) {
        // we poll events (keyboard, mouse etc.) from the system with a 20ms sleep to avoid using 100% of a CPU's core
        // and we push them into the window where the callback on the escape key will ask the windows to close
        static constexpr std::int64_t kSleepPeriodMs = 20;
        Metavision::EventLoop::poll_and_dispatch(kSleepPeriodMs);
    }

    // the recording is finished, stop the camera.
    cam.stop();
}