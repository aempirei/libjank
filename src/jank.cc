#include <jank.hh>
#include <iostream>
#include <cstring>

#include <termios.h>
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

		termios options;

		if(active)
			return false;
		device = my_device;

		fd = open(device.c_str(), O_RDWR | O_NOCTTY);
		if(fd == -1) {
			std::cerr << "unable to open " << device << ": " << strerror(errno) << std::endl;
			return false;
		}

		tcgetattr(fd, &options);

		cfsetispeed(&options, B9600);
		cfsetospeed(&options, B9600);

		options.c_cflag |= (CLOCAL | CREAD);

		options.c_cflag &= ~PARENB;
		options.c_cflag &= ~CSTOPB;
		options.c_cflag &= ~CSIZE;
		options.c_cflag |= CS8;

		options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

		options.c_cc[VMIN]  = 1;
		options.c_cc[VTIME] = 0;

		options.c_oflag &= ~OPOST;

		if(tcsetattr(fd, TCSANOW, &options) == -1) {
			std::cerr << "unable to set terminal parameters: " << strerror(errno) << std::endl;
			close(fd);
			return false;
		}

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
