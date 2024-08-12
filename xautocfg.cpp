/**
 * automatically set x keyboard repeat rate whenever a new keyboard is detected.
 *
 * (c) 2022 Jonas Jelten <jj@sft.lol>
 *
 * GPLv3 or later.
 */

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <getopt.h>

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XI2.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XKB.h>

using namespace std::literals;

struct args {
	std::string config;
	bool custom_config = false;
};

std::string expandTilde(const std::string& path) {
    if (path.empty() || path[0] != '~') {
        return path;
    }
    const char* homeDir = std::getenv("HOME");
    if (!homeDir) {
        std::cerr << "HOME environment variable not set." << std::endl;
        return path;
    }
    std::string expandedPath = homeDir;
    expandedPath += path.substr(1);
    return expandedPath;
}

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
	} keyboard;
	struct scripts {
		std::string on_enable = "";
		std::string on_disable = "";
	} scripts;
};

enum class config_section {
	none,
	keyboard,
	scripts,
};


void parse_config_entry(config *config,
                        config_section section,
                        const std::string& key,
                        const std::string& val) {
	std::istringstream vals{val};

	if (section == config_section::keyboard) {
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
	} else if (section == config_section::scripts) {
		if (key == "on_enable"sv) {
			config->scripts.on_enable = expandTilde(vals.str());
		}
		else if (key == "on_disable"sv) {
			 config->scripts.on_disable = expandTilde(vals.str());
		}
	} else if (section == config_section::none) {
		std::cout << "not in a config section: "
		          << key << " = " << val << std::endl;
		exit(1);
	} else {
		throw std::logic_error{"unknown config section"};
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
	const std::regex kv_re("^([^= ]+) *= *([^ ]+)$");
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
				} else if (section_name == "scripts") {
					current_section = config_section::scripts;
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


int main(int argc, char **argv) {
	args args = parse_args(argc, argv);

	config cfg = parse_config(args);
	std::cout << "keyboard config: "
	          << "delay=" << cfg.keyboard.delay
	          << ", interval=" << cfg.keyboard.interval
	          << "; scripts config: "
	          << "on_enable=" << cfg.scripts.on_enable
	          << ", on_disable=" << cfg.scripts.on_disable
	          << std::endl;

	std::cout << "connecting to x..." << std::endl;

	Display* display = XOpenDisplay(nullptr);

	int firstevent, error, opcode;
	if (!XQueryExtension(display, "XInputExtension", &opcode, &firstevent, &error)) {
		std::cout << "no xinput extension" << std::endl;
		return 1;
	}

	auto action = [&](int deviceid, bool enabled) {
		if (enabled) {
			// we could use XkbUseCoreKbd as deviceid to always target the core
			std::cout << "setting repeat rate on device=" << deviceid << std::endl;
			XkbSetAutoRepeatRate(display, deviceid, cfg.keyboard.delay, cfg.keyboard.interval);
		}
	};

	auto execute_script = [&](int deviceid, bool enabled) {
		if (enabled && !cfg.scripts.on_enable.empty()) {
			std::cout << "executing custom script for enabled device=" << deviceid << std::endl;
			int ret = system(cfg.scripts.on_enable.c_str());
			if (ret != 0) {
				std::cerr << "on_enable script failed: " << cfg.scripts.on_enable << ", exit code: " << ret << std::endl;
			}
		} else if (!cfg.scripts.on_disable.empty()) {
			std::cout << "executing custom script for[] disabled device=" << deviceid << std::endl;
			int ret = system(cfg.scripts.on_disable.c_str());
			if (ret != 0) {
				std::cerr << "on_disable script failed: " << cfg.scripts.on_disable << ", exit code: " << ret << std::endl;
			}
		}
	};

	// set rate at startup for core keyboard
	std::cout << "setting rate to core keyboard" << std::endl;
	action(XkbUseCoreKbd, true);

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
							action(hier->deviceid, true);
							execute_script(hier->deviceid, true);
						}
						if (hier->flags & XIDeviceDisabled) {
							action(hier->deviceid, false);
							execute_script(hier->deviceid, false);
						}
					}
				}
			}
		}
	}

	XCloseDisplay(display);
	return 0;
}
