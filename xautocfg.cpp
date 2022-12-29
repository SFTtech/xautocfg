/**
 * automatically set x keyboard repeat rate whenever a new keyboard is detected.
 *
 * (c) 2022 Jonas Jelten <jj@sft.lol>
 *
 * GPLv3 or later.
 */

#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <string_view>
#include <cstdlib>

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XI2.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XKB.h>

using namespace std::literals;

struct config {
	uint32_t delay = 200;
	uint32_t interval = 20;
};


void parse_config_entry(config *config,
                        const std::string& key,
                        const std::string& val) {
	std::istringstream vals{val};

	if (key == "delay"sv) {
		vals >> config->delay;
	}
	else if (key == "rate"sv) {
		uint32_t rate;
		vals >> rate;
		// xserver wants the repeat-interval in ms,
		// but xset r rate delay repeat rate,
		// so interval = 1000Hz / rate
		config->interval = 1000.f / rate;
	}
}


config parse_config(const std::string &filename) {
	config ret{};

	std::ifstream file{filename, std::ios::binary};
	if (not file.is_open()) {
		std::cout << "failed to open " << filename << "!\n"
		          << "using default config." << std::endl;
		return ret;
	}

	const std::regex comment_re("^([^#]*)#?.*");
	const std::regex kv_re("^ *([^= ]+) *= *([^ ]+) *");
	std::smatch comment_match;
	std::smatch kv_match;

	std::string fullline{};
	int linenr = 0;
	while (std::getline(file, fullline)) {
		linenr += 1;
		// filter comments
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

		// parse 'key = value'
		std::regex_match(line, kv_match, kv_re);
		if (not kv_match.ready() or kv_match.size() != 3) {
			std::cout << "invalid syntax in line " << linenr << ":\n"
			          << fullline << std::endl;
			exit(1);
		}

		const std::string& key{kv_match[1]};
		const std::string& val{kv_match[2]};

		parse_config_entry(&ret, key, val);
	}

	return ret;
}


int main() {
	// TODO: argparsing?

	std::ostringstream cfgpath;
	const char *home = std::getenv("HOME");
	if (not home) {
		std::cout << "HOME env not set, can't locate config." << std::endl;
		exit(1);
	}
	cfgpath << home << "/.config/xautocfg.cfg";
	config cfg = parse_config(cfgpath.str());
	std::cout << "config: "
	          << "delay=" << cfg.delay
	          << ", interval=" << cfg.interval
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
			XkbSetAutoRepeatRate(display, deviceid, cfg.delay, cfg.interval);
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
						}
						if (hier->flags & XIDeviceDisabled) {
							action(hier->deviceid, false);
						}
					}
				}
			}
		}
	}

	XCloseDisplay(display);
	return 0;
}
