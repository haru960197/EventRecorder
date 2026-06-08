#include <chrono>
#include <cstring>
#include <cstdlib>

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

// Metavision HAL headers (no Camera class)
#include <metavision/hal/device/device.h>
#include <metavision/hal/device/device_discovery.h>
#include <metavision/hal/utils/device_config.h>
#include <metavision/hal/facilities/i_events_stream.h>
#include <metavision/hal/facilities/i_events_stream_decoder.h>
#include <metavision/hal/facilities/i_geometry.h>

#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <cstdint>
#include <unistd.h>
#endif

// ============================================================
// Recording timing constants
//
// The recording consists of three phases:
//   Phase 1: [0, MARGIN_BEFORE_SEC)         — warm-up (no mask)
//   Phase 2: [MARGIN_BEFORE_SEC, MARGIN_BEFORE_SEC + user_duration)
//                                            — masked recording
//   Phase 3: last MARGIN_AFTER_SEC seconds  — cool-down (mask cleared)
//
// Total recording time = MARGIN_BEFORE_SEC + user_duration + MARGIN_AFTER_SEC
// ============================================================
static constexpr float MARGIN_BEFORE_SEC = 5.0f; // Warm-up before mask apply
static constexpr float MARGIN_AFTER_SEC = 5.0f;  // Cool-down after mask clear

// ============================================================
// V4L2 pixel mask structures for GenX320
// (Matching Prophesee driver definitions)
// ============================================================
#ifdef __linux__

struct PixelRow
{
  bool dirty;
  // 3 bytes padding inserted automatically before vectors
  uint32_t vectors[10]; // 320 bits = 32 bits * 10
};

struct PixelGrid
{
  uint32_t width;
  uint32_t height;
  PixelRow rows[320];
};

// V4L2 control IDs (using unique names to avoid collision with kernel macros)
static constexpr uint32_t PSEE_ROI_CLASS = 0x4000;
static constexpr uint32_t PSEE_CID_ROI_CTRL = V4L2_CID_USER_BASE | PSEE_ROI_CLASS;
static constexpr uint32_t GENX320_CID_ROI_PIXEL_ARRAY = PSEE_CID_ROI_CTRL + 8;

// ioctl helper
static unsigned long make_iowr(unsigned char type, unsigned int nr, size_t size)
{
  return (3UL << 30) | ((unsigned long)size << 16) | ((unsigned long)type << 8) | nr;
}

/// Find the GenX320 sensor V4L2 sub-device path.
static std::string find_sensor_subdev()
{
  const std::string base = "/sys/class/video4linux";
  DIR *dir = opendir(base.c_str());
  if (!dir)
    return "";

  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr)
  {
    std::string name_str(entry->d_name);
    if (name_str.find("v4l-subdev") == std::string::npos)
      continue;

    std::string name_path = base + "/" + name_str + "/name";
    std::ifstream ifs(name_path);
    if (!ifs.is_open())
      continue;

    std::string sensor_name;
    std::getline(ifs, sensor_name);
    ifs.close();

    // Convert to lowercase for comparison
    std::string lower = sensor_name;
    for (auto &c : lower)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lower.find("genx320") != std::string::npos ||
        lower.find("psee") != std::string::npos ||
        lower.find("imx636") != std::string::npos)
    {
      closedir(dir);
      std::string dev_path = "/dev/" + name_str;
      std::cout << "[INFO] Discovered sensor subdevice: " << dev_path
                << " (" << sensor_name << ")" << std::endl;
      return dev_path;
    }
  }
  closedir(dir);
  return "";
}

/// Apply V4L2 pixel mask to disable hot pixels on the sensor.
static bool apply_hotpixel_mask(const std::vector<std::pair<int, int>> &hot_pixels)
{
  std::string dev_path = find_sensor_subdev();
  if (dev_path.empty())
  {
    dev_path = "/dev/v4l-subdev0";
    std::cerr << "[WARN] Sensor subdevice not found. Falling back to: " << dev_path << std::endl;
  }

  // Build the pixel grid: all pixels active (bit=1), then clear hot pixel bits
  PixelGrid grid{};
  grid.width = 320;
  grid.height = 320;

  for (int y = 0; y < 320; ++y)
  {
    grid.rows[y].dirty = false;
    for (int x = 0; x < 10; ++x)
    {
      grid.rows[y].vectors[x] = 0xFFFFFFFF;
    }
  }

  int masked_count = 0;
  for (const auto &[px, py] : hot_pixels)
  {
    if (px < 0 || px >= 320 || py < 0 || py >= 320)
    {
      std::cerr << "[WARN] Hot pixel (" << px << ", " << py
                << ") is out of range [0, 320). Skipping." << std::endl;
      continue;
    }
    int vector_idx = px / 32;
    int bit_idx = px % 32;
    grid.rows[py].vectors[vector_idx] &= ~(1u << bit_idx);
    grid.rows[py].dirty = true;
    ++masked_count;
  }

  std::cout << "[INFO] Masking " << masked_count << " hot pixel(s) via V4L2." << std::endl;

  // Prepare V4L2 ext controls
  struct v4l2_ext_control ctrl{};
  ctrl.id = GENX320_CID_ROI_PIXEL_ARRAY;
  ctrl.size = sizeof(grid);
  ctrl.ptr = &grid;

  struct v4l2_ext_controls ctrls{};
  ctrls.which = 0;
  ctrls.count = 1;
  ctrls.controls = &ctrl;

  unsigned long VIDIOC_S_EXT_CTRLS_CMD = make_iowr('V', 72, sizeof(ctrls));

  int fd = open(dev_path.c_str(), O_RDWR);
  if (fd < 0)
  {
    std::cerr << "[ERROR] Failed to open V4L2 device: " << dev_path
              << " (errno=" << errno << ")" << std::endl;
    return false;
  }

  int ret = ioctl(fd, VIDIOC_S_EXT_CTRLS_CMD, &ctrls);
  close(fd);

  if (ret < 0)
  {
    std::cerr << "[ERROR] V4L2 ioctl VIDIOC_S_EXT_CTRLS failed (errno=" << errno << ")." << std::endl;
    return false;
  }

  std::cout << "[INFO] Hot pixel mask applied successfully." << std::endl;
  return true;
}

#endif // __linux__

// ============================================================
// Hot pixel JSON loader (minimal hand-written parser)
// ============================================================

/// Resolve the path to config/hot_pixels.json relative to the executable.
static std::string resolve_config_path(const char *argv0)
{
  // Try relative to the working directory first
  // (the user is expected to run from the project root or build dir)
  std::vector<std::string> candidates = {
      "config/hot_pixels.json",
      "../config/hot_pixels.json",
  };

  // Also try relative to the executable path
  std::string exe_dir;
  {
    std::string argv0_str(argv0);
    auto pos = argv0_str.rfind('/');
    if (pos != std::string::npos)
    {
      exe_dir = argv0_str.substr(0, pos);
      candidates.push_back(exe_dir + "/../config/hot_pixels.json");
    }
  }

  for (const auto &path : candidates)
  {
    std::ifstream test(path);
    if (test.good())
      return path;
  }
  return "";
}

/// Parse config/hot_pixels.json and extract (x, y) pairs.
/// Minimal parser: looks for "x": <int> and "y": <int> patterns within each object.
static bool load_hot_pixels(const std::string &path, std::vector<std::pair<int, int>> &out)
{
  std::ifstream ifs(path);
  if (!ifs.is_open())
  {
    std::cerr << "[ERROR] Cannot open hot pixel config: " << path << std::endl;
    return false;
  }

  std::string content((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
  ifs.close();

  // Simple state-machine parser for JSON array of {"x": N, "y": M} objects
  // We look for pairs of "x" and "y" values within brace-delimited objects
  out.clear();

  size_t pos = 0;
  while (pos < content.size())
  {
    // Find next '{'
    pos = content.find('{', pos);
    if (pos == std::string::npos)
      break;

    // Find matching '}'
    size_t end = content.find('}', pos);
    if (end == std::string::npos)
      break;

    std::string obj = content.substr(pos, end - pos + 1);
    pos = end + 1;

    // Extract "x": <int>
    int x_val = -1, y_val = -1;
    bool has_x = false, has_y = false;

    auto extract_int = [&](const std::string &key) -> std::pair<bool, int>
    {
      std::string pattern = "\"" + key + "\"";
      size_t kp = obj.find(pattern);
      if (kp == std::string::npos)
        return {false, 0};
      kp += pattern.size();
      // Skip whitespace and colon
      while (kp < obj.size() && (obj[kp] == ' ' || obj[kp] == ':' || obj[kp] == '\t' || obj[kp] == '\n' || obj[kp] == '\r'))
        ++kp;
      // Parse integer
      if (kp >= obj.size())
        return {false, 0};
      char *endp = nullptr;
      long val = std::strtol(obj.c_str() + kp, &endp, 10);
      if (endp == obj.c_str() + kp)
        return {false, 0};
      return {true, static_cast<int>(val)};
    };

    auto [xok, xv] = extract_int("x");
    auto [yok, yv] = extract_int("y");

    if (xok && yok)
    {
      out.emplace_back(xv, yv);
    }
  }

  std::cout << "[INFO] Loaded " << out.size() << " hot pixel(s) from: " << path << std::endl;
  return !out.empty() || content.find("\"hot_pixels\"") != std::string::npos;
}

// ============================================================
// Usage
// ============================================================
static void print_usage(const char *prog)
{
  std::cout << "Usage: " << prog << " [OPTIONS]\n"
            << "\n"
            << "Options:\n"
            << "  -t, --time <seconds>     Recording duration in seconds (default: 15.0)\n"
            << "                           This is the masked recording time.\n"
            << "                           Total time = " << MARGIN_BEFORE_SEC << "s (warm-up) + duration + " << MARGIN_AFTER_SEC << "s (cool-down)\n"
            << "  -o, --output <file>      Output RAW file path (default: events.raw)\n"
            << "  -h, --help               Show this help message\n"
            << std::endl;
}

// ============================================================
// main
// ============================================================
int main(int argc, char *argv[])
{
  float recording_duration = 15.0f;
  std::string output_path = "events.raw";

  // ---- Parse command-line arguments ----
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
      recording_duration = std::strtof(argv[++i], &end_ptr);
      if (end_ptr == argv[i] || *end_ptr != '\0' || recording_duration <= 0.0f)
      {
        std::cerr << "Invalid duration: '" << argv[i] << "'. Please provide a positive number in seconds." << std::endl;
        return 1;
      }
    }
    else if (std::strcmp(argv[i], "-o") == 0 || std::strcmp(argv[i], "--output") == 0)
    {
      if (i + 1 >= argc)
      {
        std::cerr << "Missing value for -o/--output option." << std::endl;
        return 1;
      }
      output_path = argv[++i];
    }
    else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0)
    {
      print_usage(argv[0]);
      return 0;
    }
    else if (argv[i][0] == '-')
    {
      std::cerr << "Unknown option: '" << argv[i] << "'. Use -h for help." << std::endl;
      return 1;
    }
    else
    {
      std::cerr << "Unexpected positional argument: '" << argv[i] << "'. Use -h for help." << std::endl;
      return 1;
    }
  }

  // Compute phase boundaries
  const float total_duration = MARGIN_BEFORE_SEC + recording_duration + MARGIN_AFTER_SEC;
  const float mask_apply_time = MARGIN_BEFORE_SEC;
  const float mask_clear_time = MARGIN_BEFORE_SEC + recording_duration;

  std::cout << "[INFO] Recording duration: " << recording_duration << "s (masked)" << std::endl;
  std::cout << "[INFO] Total recording time: " << total_duration << "s "
            << "(warm-up " << MARGIN_BEFORE_SEC << "s + recording " << recording_duration << "s + cool-down " << MARGIN_AFTER_SEC << "s)" << std::endl;

  // ---- 1. Open device via Metavision HAL (no Camera class) ----
  std::cout << "[INFO] Opening camera via Metavision HAL..." << std::endl;
  Metavision::DeviceConfig device_config;
  device_config.enable_biases_range_check_bypass(true);

  std::unique_ptr<Metavision::Device> device;
  try
  {
    device = Metavision::DeviceDiscovery::open("", device_config);
  }
  catch (const std::exception &e)
  {
    std::cerr << "[ERROR] Exception during device discovery: " << e.what() << std::endl;
    return 1;
  }

  if (!device)
  {
    std::cerr << "[ERROR] Failed to open camera via Metavision HAL." << std::endl;
    return 1;
  }
  std::cout << "[INFO] Camera successfully opened via HAL." << std::endl;

  // Obtain HAL facilities
  auto *i_events_stream = device->get_facility<Metavision::I_EventsStream>();
  auto *i_stream_decoder = device->get_facility<Metavision::I_EventsStreamDecoder>();
  auto *i_geometry = device->get_facility<Metavision::I_Geometry>();

  if (!i_events_stream)
  {
    std::cerr << "[ERROR] I_EventsStream facility not available." << std::endl;
    return 1;
  }

  int sensor_width = i_geometry ? i_geometry->get_width() : 320;
  int sensor_height = i_geometry ? i_geometry->get_height() : 320;
  std::cout << "[INFO] Sensor geometry: " << sensor_width << " x " << sensor_height << std::endl;

  // ---- 2. Load hot pixel mask (to be applied dynamically later) ----
  std::vector<std::pair<int, int>> hot_pixels;
#ifndef __linux__
  std::cerr << "[WARN] Hot pixel masking is only supported on Linux (V4L2)." << std::endl;
#else
  std::string config_path = resolve_config_path(argv[0]);
  if (!config_path.empty())
  {
    if (load_hot_pixels(config_path, hot_pixels))
    {
      if (hot_pixels.empty())
      {
        std::cout << "[INFO] Hot pixel config loaded, but it is empty." << std::endl;
      }
    }
    else
    {
      std::cerr << "[WARN] Failed to load hot pixel data from: " << config_path << std::endl;
    }
  }
  else
  {
    std::cerr << "[WARN] Hot pixel config not found (config/hot_pixels.json). "
              << "Phase 2 will not apply any mask." << std::endl;
  }
#endif

  // ---- 3. Start RAW file logging (writes EVT3 header automatically) ----
  i_events_stream->log_raw_data(output_path);
  std::cout << "[INFO] Recording RAW data to: " << output_path << std::endl;

  // ---- 4. Start event stream ----
  i_events_stream->start();

  // ---- 5. Recording loop (3-phase) ----
  const auto start_time = std::chrono::steady_clock::now();

  bool mask_applied = false;
  bool mask_cleared = false;
  std::atomic<bool> running{true};

  std::cout << "[INFO] Recording started (Total " << total_duration << " seconds)..." << std::endl;
  std::cout << "[PHASE 1] 0-" << mask_apply_time << "s: Warm-up (No Mask)" << std::endl;

  while (running.load())
  {
    // Poll for available data
    short status = i_events_stream->poll_buffer();
    if (status < 0)
    {
      std::cerr << "[WARN] Event stream ended unexpectedly (poll returned " << status << ")." << std::endl;
      break;
    }

    if (status > 0)
    {
      // Retrieve and decode the latest raw data buffer.
      // File writing is handled automatically by log_raw_data().
      auto raw_data = i_events_stream->get_latest_raw_data();
      if (raw_data && i_stream_decoder)
      {
        i_stream_decoder->decode(raw_data);
      }
    }

    // Compute elapsed time
    const auto elapsed = std::chrono::steady_clock::now() - start_time;
    float seconds = std::chrono::duration<float>(elapsed).count();

    // Phase 2: Apply hot pixel mask after warm-up margin
    if (seconds >= mask_apply_time && !mask_applied)
    {
      std::cout << "\n[PHASE 2] " << mask_apply_time << "-" << mask_clear_time
                << "s: Masked Recording (Hot Pixel Mask ON)" << std::endl;
#ifdef __linux__
      if (!hot_pixels.empty())
      {
        apply_hotpixel_mask(hot_pixels);
      }
      else
      {
        std::cout << "[WARN] Masking skipped (no hot pixels loaded)" << std::endl;
      }
#endif
      mask_applied = true;
    }

    // Phase 3: Clear mask before cool-down margin
    if (seconds >= mask_clear_time && !mask_cleared)
    {
      std::cout << "\n[PHASE 3] " << mask_clear_time << "-" << total_duration
                << "s: Cool-down (Hot Pixel Mask OFF)" << std::endl;
#ifdef __linux__
      if (!hot_pixels.empty())
      {
        // Pass an empty list to clear the mask (all pixels enabled)
        std::vector<std::pair<int, int>> empty_list;
        apply_hotpixel_mask(empty_list);
      }
      else
      {
        std::cout << "[INFO] Mask clear skipped (no mask was applied)" << std::endl;
      }
#endif
      mask_cleared = true;
    }

    // End recording after total duration
    if (seconds >= total_duration)
    {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // ---- 6. Stop stream and close log file ----
  i_events_stream->stop();
  i_events_stream->stop_log_raw_data();

  std::cout << "[INFO] Recording finished." << std::endl;
  std::cout << "[INFO] Output file: " << output_path << std::endl;

  return 0;
}