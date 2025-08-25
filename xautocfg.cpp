/**
 * automatically set x keyboard repeat rate whenever a new keyboard is detected.
 *
 * (c) 2022-2024 Jonas Jelten <jj@sft.lol>
 *
 * GPLv3 or later.
 */

#include <algorithm>
#include <format>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <regex>
#include <unordered_map>
#include <sys/wait.h>

#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>

using namespace std::literals;


struct args {
	std::string config;
	bool custom_config = false;
};


args parse_args(int argc, char** argv) {
	args ret;

	int c;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"help",    no_argument,       0, 'h'},
			{"config",  required_argument, 0, 'c'},
			{0,         0,                 0,  0 }
		};

		c = getopt_long(argc, argv, "c:h",
		                long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h': {
			std::cout << "usage: " << argv[0] << " [OPTION]...\n"
			          << "\n"
			          << "automatically set properties for newly connected X devices.\n"
			          << "\n"
			          << "Options:\n"
			          << "   -h, --help                 show this help\n"
			          << "   -c, --config=FILE          use this config file instead of ~/.config/xautocfg.cfg\n"
			          << std::endl;

			const option *op = nullptr;
			for (size_t i = 0; ; op = &long_options[i++]) {
				if (op->name == nullptr and op->has_arg == 0
				    and op->flag == nullptr and op->val == 0) {
					break;
				}
			}
			exit(0);
			break;
		}

		case 'c':
			ret.config = std::string{optarg};
			ret.custom_config = true;
			break;
		}
	}

	if (optind < argc) {
		std::cout << "invalid non-option arguments" << std::endl;
		exit(1);
	}

	// set defaults
	if (not ret.config.size()) {
		std::ostringstream cfgpathss;
		const char *home = std::getenv("HOME");
		if (not home) {
			std::cout << "HOME env not set, can't locate config." << std::endl;
			exit(1);
		}
		cfgpathss << home << "/.config/xautocfg.cfg";
		ret.config = std::move(cfgpathss).str();
	}

	return ret;
}


struct config {
	struct keyboard {
		uint32_t delay = 200;
		uint32_t interval = 20;
		std::string on_connect = "";
		std::string on_disconnect = "";
	} keyboard;
};

enum class config_section {
	none,
	keyboard,
};


void parse_config_entry(config *config,
                        config_section section,
                        const std::string& key,
                        const std::string& val) {
	std::istringstream vals{val};

	switch (section) {
	case config_section::keyboard:
		if (key == "delay"sv) {
			vals >> config->keyboard.delay;
		}
		else if (key == "rate"sv) {
			uint32_t rate;
			vals >> rate;
			// xserver wants the repeat-interval in ms,
			// but xset r rate delay repeat rate,
			// so interval = 1000Hz / rate
			config->keyboard.interval = 1000.f / rate;
		}
		else if (key == "on_connect"sv) {
			config->keyboard.on_connect = val;
		}
		else if (key == "on_disconnect"sv) {
			config->keyboard.on_disconnect = val;
		}
		else {
			throw std::logic_error{std::format("unknown keyboard section entry: {}", key)};
		}
		break;
	case config_section::none:
		std::cout << "not in a config section: "
		          << key << " = " << val << std::endl;
		exit(1);
		break;
	}
}


config parse_config(const args &args) {
	config ret{};

	std::ifstream file{args.config, std::ios::binary};
	if (not file.is_open()) {
		std::cout << "failed to open config file '" << args.config << "'!" << std::endl;
		if (args.custom_config) {
			exit(1);
		}
		std::cout << "using default config." << std::endl;
		return ret;
	}


	const std::regex comment_re("^ *([^#]*) *#?.*");
	const std::regex section_re("^\\[([^\\]]+)\\]$");
	const std::regex kv_re("^([^= ]+) *= *(.+)$");
	config_section current_section = config_section::none;

	std::string fullline{};
	int linenr = 0;
	while (std::getline(file, fullline)) {
		linenr += 1;
		// filter comments
		std::smatch comment_match;
		std::regex_match(fullline, comment_match, comment_re);
		if (not comment_match.ready() or comment_match.size() != 2) {
			std::cout << "error in config file line " << linenr << ":\n"
			          << fullline << std::endl;
			exit(1);
		}

		const std::string& line{comment_match[1]};

		// filter empty lines
		if (line.size() == 0 or std::ranges::all_of(line, [](const char c) {
			return c == ' ';
		})) {
			continue;
		}

		// parse '[section]'
		{
			std::smatch match;
			std::regex_match(line, match, section_re);
			if (match.ready() and match.size() == 2) {
				const std::string& section_name{match[1]};
				if (section_name == "keyboard") {
					current_section = config_section::keyboard;
				}
				else {
					std::cout << "unknown section name: " << fullline << std::endl;
					exit(1);
				}
				continue;
			}
		}

		// parse 'key = value'
		{
			std::smatch match;
			std::regex_match(line, match, kv_re);
			if (match.ready() and match.size() == 3) {
				const std::string& key{match[1]};
				const std::string& val{match[2]};

				parse_config_entry(&ret, current_section, key, val);
				continue;
			}
		}

		std::cout << "invalid syntax in line " << linenr << ":\n"
		          << fullline << std::endl;
		exit(1);
	}

	return ret;
}


int exec_script(const std::string &command,
                const std::unordered_map<std::string, std::string> &add_environment) {
	pid_t pid = fork();

	if (pid == -1) {
		// failed to fork
		std::cerr << "failed to fork for command " << command << std::endl;
	}
	else if (pid == 0) {
		// in child process

		// add new environment entries
		for (auto &&entry : add_environment) {
			setenv(entry.first.c_str(), entry.second.c_str(), true);
		}

		int ret = execlp("/bin/sh", "sh", "-c", command.c_str(), nullptr);
		if (ret == -1) {
			perror("failed to execute script");
		}
	}
	else {
		// in parent process
		int status;
		waitpid(pid, &status, 0);

		if (WIFEXITED(status)) {
			return WEXITSTATUS(status);
		}
		else {
			return -1;
		}
	}
	return -1;
}


int main(int argc, char **argv) {
	args args = parse_args(argc, argv);

	config cfg = parse_config(args);
	std::cout << "keyboard config: "
	          << "delay=" << cfg.keyboard.delay
	          << ", interval=" << cfg.keyboard.interval
	          << ", on_connect='" << cfg.keyboard.on_connect
	          << "', on_disconnect='" << cfg.keyboard.on_disconnect
	          << "'" << std::endl;

	std::cout << "connecting to x..." << std::endl;

	Display* display = XOpenDisplay(nullptr);

	int firstevent, error, opcode;
	if (!XQueryExtension(display, "XInputExtension", &opcode, &firstevent, &error)) {
		std::cout << "no xinput extension" << std::endl;
		return 1;
	}

	auto set_kbd_repeat_rate = [&](int deviceid, bool enabled) {
		if (enabled) {
			// we could use XkbUseCoreKbd as deviceid to always target the core
			std::cout << "setting repeat rate on device=" << deviceid << std::endl;
			XkbSetAutoRepeatRate(display, deviceid, cfg.keyboard.delay, cfg.keyboard.interval);
		}
	};

	auto run_kbd_plug_script = [&](int deviceid, bool enabled) {
		auto& command = enabled ? cfg.keyboard.on_connect : cfg.keyboard.on_disconnect;

		if (not command.empty()) {
			auto script_ret = exec_script(
				command,
				{{"XINPUTID", std::format("{}", deviceid)}});

			if (script_ret != 0) {
				std::cerr << "script failed: '" << command << "' exited with " << script_ret << std::endl;
			}
		}
	};

	auto handle_keyboard_plug = [&](int deviceid, bool enabled) {
		set_kbd_repeat_rate(deviceid, enabled);
		run_kbd_plug_script(deviceid, enabled);
	};

	// set rate at startup for core keyboard
	std::cout << "setting rate to core keyboard..." << std::endl;
	set_kbd_repeat_rate(XkbUseCoreKbd, true);

	{
		XIEventMask mask;
		mask.deviceid = XIAllDevices;
		mask.mask_len = XIMaskLen(XI_HierarchyChanged);
		auto maskdata = std::make_unique<unsigned char[]>(mask.mask_len);
		mask.mask = maskdata.get();
		XISetMask(mask.mask, XI_HierarchyChanged);
		XISelectEvents(display, DefaultRootWindow(display), &mask, 1);
	}

	XFlush(display);

	std::cout << "processing events..." << std::endl;
	while (true) {
		XEvent event;
		XNextEvent(display, &event);

		if (event.type == GenericEvent && event.xcookie.extension == opcode) {
			if (event.xcookie.evtype == XI_HierarchyChanged) {
				if (!XGetEventData(display, &event.xcookie))
					continue;

				XIHierarchyEvent *hev = reinterpret_cast<XIHierarchyEvent*>(event.xcookie.data);
				if (!(hev->flags & (XIDeviceEnabled | XIDeviceDisabled)))
					continue;

				for (ssize_t i = 0; i < hev->num_info; i++) {
					XIHierarchyInfo *hier = &hev->info[i];
					if (hier->use == XISlaveKeyboard) {
						if (hier->flags & XIDeviceEnabled) {
							handle_keyboard_plug(hier->deviceid, true);
						}
						if (hier->flags & XIDeviceDisabled) {
							handle_keyboard_plug(hier->deviceid, false);
						}
					}
				}
			}
		}
	}

	XCloseDisplay(display);
	return 0;
}
