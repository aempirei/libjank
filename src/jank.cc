#include <jank.hh>
#include <iostream>
#include <cstring>

#include <unistd.h>
#include <fcntl.h>

namespace jank {

	msr::msr() : active(false) {
	}

	msr::~msr() {

		if(active)
			stop();
	}

	bool msr::start(const char *my_device) {

		if(active)
			return false;
		device = my_device;

		fd = open(device.c_str(), O_RDWR | O_NOCTTY);
		if(fd == -1) {
			std::cerr << "unable to open " << device << ": " << strerror(errno) << std::endl;
			return false;
		}

		// fcntl(fd, F_SETFL, 0);

		active = true;

		return true;
	}

	bool  msr::stop() {

		if(not active)
			return false;

		close(fd);
		active = false;

		return true;
	}
}
