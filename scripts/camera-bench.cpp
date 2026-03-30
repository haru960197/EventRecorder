#include <metavision/sdk/stream/camera.h>

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

bool parse_duration_seconds(int argc, char *argv[], float &duration_seconds) {
	duration_seconds = 1.0f;

	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "-t") == 0 || std::strcmp(argv[i], "--time") == 0) {
			if (i + 1 >= argc) {
				std::cerr << "Missing value for -t/--time option." << std::endl;
				return false;
			}

			char *end_ptr = nullptr;
			duration_seconds = std::strtof(argv[++i], &end_ptr);
			if (end_ptr == argv[i] || *end_ptr != '\0' || duration_seconds <= 0.0f) {
				std::cerr << "Invalid duration: '" << argv[i]
									<< "'. Please provide a positive float in seconds." << std::endl;
				return false;
			}
		} else if (argv[i][0] == '-') {
			std::cerr << "Unknown option: '" << argv[i] << "'. Supported option: -t <seconds>." << std::endl;
			return false;
		} else {
			std::cerr << "Unexpected positional argument: '" << argv[i] << "'. Use -t <seconds>." << std::endl;
			return false;
		}
	}

	return true;
}

void print_result_row(const std::string &label, const std::string &value) {
	std::cout << "| " << std::left << std::setw(44) << label
						<< " | " << std::right << std::setw(18) << value << " |" << std::endl;
}

}  // namespace

int main(int argc, char *argv[]) {
	const auto program_start = std::chrono::steady_clock::now();

	float duration_seconds = 1.0f;
	if (!parse_duration_seconds(argc, argv, duration_seconds)) {
		return 1;
	}

	const std::string output_raw_path = "events.raw";

	try {
		Metavision::DeviceConfig device_config;
		device_config.set_format("EVT3");
		auto cam = Metavision::Camera::from_first_available(device_config);

		cam.start_recording(output_raw_path);
		cam.start();

		const auto recording_start = std::chrono::steady_clock::now();
		const auto max_duration = std::chrono::duration<float>(duration_seconds);

		while (cam.is_running()) {
			const auto elapsed = std::chrono::steady_clock::now() - recording_start;
			if (elapsed >= max_duration) {
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		cam.stop();
		cam.stop_recording();

		const auto recording_end = std::chrono::steady_clock::now();

		const auto recording_elapsed_s = std::chrono::duration<double>(recording_end - recording_start).count();
		std::uintmax_t raw_file_size_bytes = 0;
		if (std::filesystem::exists(output_raw_path)) {
			raw_file_size_bytes = std::filesystem::file_size(output_raw_path);
		} else {
			std::cerr << "Warning: output RAW file not found: " << output_raw_path << std::endl;
		}

		const auto program_end = std::chrono::steady_clock::now();
		const auto program_elapsed_s = std::chrono::duration<double>(program_end - program_start).count();

		std::cout << "+----------------------------------------------+--------------------+" << std::endl;
		std::cout << "| Item                                         | Value              |" << std::endl;
		std::cout << "+----------------------------------------------+--------------------+" << std::endl;

		std::ostringstream requested;
		requested << std::fixed << std::setprecision(3) << duration_seconds << " s";
		print_result_row("Requested recording duration (T)", requested.str());

		std::ostringstream actual;
		actual << std::fixed << std::setprecision(6) << recording_elapsed_s << " s";
		print_result_row("Actual elapsed time from recording start to stop", actual.str());

		std::ostringstream total;
		total << std::fixed << std::setprecision(6) << program_elapsed_s << " s";
		print_result_row("Total program elapsed time", total.str());

		std::ostringstream file_size;
		file_size << raw_file_size_bytes << " bytes";
		print_result_row("RAW file size", file_size.str());

		std::cout << "+----------------------------------------------+--------------------+" << std::endl;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
