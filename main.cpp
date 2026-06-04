#include <metavision/sdk/stream/camera.h>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <cstdint>
#include <unistd.h>
#endif

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
            << "  -t, --time <seconds>     Recording duration in seconds (default: 1.0)\n"
            << "  -m, --mask-hotpixels     Disable hot pixels before recording\n"
            << "                           (requires config/hot_pixels.json)\n"
            << "  -h, --help               Show this help message\n"
            << std::endl;
}

// ============================================================
// main
// ============================================================
int main(int argc, char *argv[])
{
  float duration_seconds = 1.0f;
  bool mask_hotpixels = false;

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
    else if (std::strcmp(argv[i], "-m") == 0 || std::strcmp(argv[i], "--mask-hotpixels") == 0)
    {
      mask_hotpixels = true;
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

  // ---- Camera setup and recording ----
  Metavision::Camera cam;

  Metavision::DeviceConfig device_config;
  device_config.set_format("EVT3");
  cam = Metavision::Camera::from_first_available(device_config);

  // ---- Hot pixel masking (V4L2 direct) ----
  if (mask_hotpixels)
  {
#ifndef __linux__
    std::cerr << "[ERROR] Hot pixel masking is only supported on Linux (V4L2)." << std::endl;
    return 1;
#else
    std::string config_path = resolve_config_path(argv[0]);
    if (config_path.empty())
    {
      std::cerr << "[ERROR] Hot pixel config not found." << std::endl;
      std::cerr << "        Expected: config/hot_pixels.json" << std::endl;
      std::cerr << "        Run scripts/calibrate_hot_pixels.sh to generate it." << std::endl;
      return 1;
    }

    std::vector<std::pair<int, int>> hot_pixels;
    if (!load_hot_pixels(config_path, hot_pixels))
    {
      std::cerr << "[ERROR] Failed to load hot pixel data from: " << config_path << std::endl;
      return 1;
    }

    if (hot_pixels.empty())
    {
      std::cout << "[INFO] No hot pixels found in config. Proceeding without masking." << std::endl;
    }
    else
    {
      if (!apply_hotpixel_mask(hot_pixels))
      {
        std::cerr << "[ERROR] Failed to apply hot pixel mask. Aborting." << std::endl;
        return 1;
      }
    }
#endif
  }

  cam.start_recording("events.raw");

  cam.start();
  std::cout << "[INFO] Recording started." << std::endl;

  const auto start_time = std::chrono::steady_clock::now();
  const auto max_duration = std::chrono::duration<float>(duration_seconds);

  while (cam.is_running())
  {
    const auto elapsed = std::chrono::steady_clock::now() - start_time;
    if (elapsed >= max_duration)
    {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  cam.stop();
  cam.stop_recording();
  std::cout << "[INFO] Recording finished." << std::endl;
}