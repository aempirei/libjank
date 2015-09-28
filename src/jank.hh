#pragma once

#include <string>

namespace jank {
	struct msr;


	struct msr {

		std::string device;
		bool active;
		int fd;

		bool start(const char *);
		bool start();
		bool stop();
		msr();
		~msr();
	};
}
