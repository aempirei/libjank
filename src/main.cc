#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>

#include <unistd.h>

#include <jank.hh>

void usage(char *);

struct config {
	bool verbose = false;
	bool test = false;
	char *msr_filename = nullptr;
};

int main(int argc, char **argv) {

	jank::msr msr;

	config c;

	int opt;

	while((opt = getopt(argc, argv, "vtd:h")) != -1) {
		switch(opt) {
			case 'v':
				c.verbose = true;
				break;
			case 't':
				c.test = true;
				break;
			case 'd':
				c.msr_filename = optarg;
				break;
			case 'h':
			default:
				usage(argv[0]);
				return -1;
		}
	}

	if(c.msr_filename == nullptr) {
		std::cerr << "msr device filename not specified (use -d flag)" << std::endl;
		return -1;
	}

	msr.start(c.msr_filename);

	msr.stop();

	return 0;
}

void usage(char *arg0) {
	std::cout << std::endl << "usage: " << arg0 << " [options] -d device" << std::endl << std::endl;
	std::cout << "\t-t          test mode" << std::endl;
	std::cout << "\t-v          verbose output" << std::endl;
	std::cout << "\t-h          show this help" << std::endl;
	std::cout << "\t-d device   filename of MSR-605 device" << std::endl;
	std::cout << std::endl;
}
