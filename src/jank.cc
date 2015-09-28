#include <jank.hh>

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
		active = true;
		return true;
	}

	bool  msr::stop() {
		if(not active)
			return false;
		active = false;
		return true;
	}
}
