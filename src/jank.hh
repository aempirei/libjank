#pragma once

#include <string>

namespace jank {
	struct msr;


	struct msr {
		std::string device;
		bool active;
		bool start(const char *);
		bool stop();
		msr();
		~msr();
	};
}
