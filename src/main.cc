#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>

#include <cstring>

#include <unistd.h>

#include <jank.hh>

#define ESC "\x1b"
#define msleep(X) usleep((X) * 1000)

void usage(char *);

void printresp(const char *, size_t);

struct config {
	bool verbose = false;
	bool test = false;
	char *msr_filename = nullptr;
};

int main(int argc, char **argv) {

	jank::msr msr;

	config c;

	char buffer[256];

	int opt;

	int n;

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

	if(c.verbose) {
		std::cout << "verbose output mode enabled" << std::endl;
		std::cout << "msr device at " << c.msr_filename << std::endl;
	}

	if(msr.start(c.msr_filename) == false) {
		std::cerr << "failed to start device " << c.msr_filename << std::endl;
		return -1;
	}

	if(c.verbose)
		std::cout << "Issuing RESET command" << std::endl;

	if(write(msr.fd, ESC "a", 2) != 2) perror("RESET command failed on write()");

	if(write(msr.fd, ESC "\x83", 2) != 2) perror("Green LED On failed on write()"); msleep(100);
	if(write(msr.fd, ESC "\x84", 2) != 2) perror("Yellow LED On failed on write()"); msleep(100);
	if(write(msr.fd, ESC "\x85", 2) != 2) perror("Red LED On failed on write()"); msleep(100);
	if(write(msr.fd, ESC "\x84", 2) != 2) perror("Yellow LED On failed on write()"); msleep(100);
	if(write(msr.fd, ESC "\x83", 2) != 2) perror("Green LED On failed on write()"); msleep(100);

	if(write(msr.fd, ESC "\x81", 2) != 2) perror("All LEDs Off failed on write()");

	if(c.test) {

		if(c.verbose)
			std::cout << "entering test mode" << std::endl;

		//
		// RAM test
		//

		if(c.verbose)
			std::cout << "Performing RAM test" << std::endl;

		if(write(msr.fd, ESC "\x87", 2) != 2)
			perror("RAM test failed on write()");

		n = read(msr.fd, buffer, sizeof(buffer));
		if(n == -1) {
			perror("RAM test failed on read()");
		} else {
			if(n == 2 and memcmp(buffer, ESC "0", 2) == 0)
				std::cout << "passed." << std::endl;
			else
				std::cout << "failed." << std::endl;
			printresp(buffer, n);
		}

		//
		// communication test
		//

		if(c.verbose)
			std::cout << "Performing communication test" << std::endl;

		if(write(msr.fd, ESC "e", 2) != 2)
			perror("Communication test failed on write()");

		n = read(msr.fd, buffer, sizeof(buffer));
		if(n == -1) {
			perror("Communication test failed on read()");
		} else {
			if(n == 2 and memcmp(buffer, ESC "y", 2) == 0)
				std::cout << "passed." << std::endl;
			else
				std::cout << "failed." << std::endl;
			printresp(buffer, n);
		}

		//
		// sensor test
		//

		if(c.verbose)
			std::cout << "Performing sensor test" << std::endl;

		if(write(msr.fd, ESC "\x86", 2) != 2)
			perror("Sensor test failed on write()");

			std::cout << "Please swipe a card." << std::endl;

		n = read(msr.fd, buffer, sizeof(buffer));
		if(n == -1) {
			perror("Sensor test failed on read()");
		} else {
			if(n == 2 and memcmp(buffer, ESC "0", 2) == 0)
				std::cout << "passed." << std::endl;
			else
				std::cout << "failed." << std::endl;
			printresp(buffer, n);
		}

	}

	msr.stop();

	if(c.verbose)
		std::cout << "goodbye." << std::endl;

	return 0;
}

void printresp(const char *resp, size_t resp_sz) {

	std::cout << std::dec << resp_sz << " bytes:";

	for(unsigned int i = 0; i < resp_sz; i++)
		std::cout << ' ' << std::hex << std::setfill('0') << std::setw(2) << (int)resp[i];

	std::cout << std::endl;
}

void usage(char *arg0) {
	std::cout << std::endl << "usage: " << arg0 << " [options] -d device" << std::endl << std::endl;
	std::cout << "\t-t          test mode" << std::endl;
	std::cout << "\t-v          verbose output" << std::endl;
	std::cout << "\t-h          show this help" << std::endl;
	std::cout << "\t-d device   filename of MSR-605 device" << std::endl;
	std::cout << std::endl;
}
