#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>

#include <cstring>
#include <cstdio>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <signal.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <jank.hh>

namespace config {

	bool verbose = true;
	bool info = false;
	bool test = false;
	bool cli = false;

	const char *glob = "/dev/ttyUSB*";
	const char *device = "/dev/ttyUSB0";

	int argc;
	char **argv;

	jank::msr msr;

	void usage() {

		std::string prog = basename(argv[0]);

		std::cout << std::endl << "usage: " << prog << " [options] -d device" << std::endl << std::endl;

		std::cout << "\t-h          show this help" << std::endl;

		std::cout << "\t-t          toggle test mode (default="         << (test    ? "ENABLED" : "DISABLED") << ")" << std::endl;
		std::cout << "\t-v          toggle verbose mode (default="      << (verbose ? "ENABLED" : "DISABLED") << ")" << std::endl;
		std::cout << "\t-i          toggle info mode (default="         << (info    ? "ENABLED" : "DISABLED") << ")" << std::endl;
		std::cout << "\t-c          toggle command-line mode (default=" << (cli     ? "ENABLED" : "DISABLED") << ")" << std::endl;

		std::cout << "\t-d device   filename of MSR-605 device";

		if(device != nullptr)
			std::cout << " (default=" << device << ")";

		std::cout << std::endl;

	}

	void init(int my_argc, char **my_argv) {

		argc = my_argc;
		argv = my_argv;
	}

	bool parse() {

		int opt;

		while((opt = getopt(argc, argv, "hvitcd:")) != -1) {

			switch(opt) {

				case 'v': verbose = not verbose ; break;
				case 'i': info    = not info    ; break;
				case 't': test    = not test    ; break;
				case 'c': cli     = not cli     ; break;

				case 'd':
						  device = optarg;
						  break;

				case 'h':
				default:
						  return false;
			}
		}

		return true;
	}
}

volatile sig_atomic_t done = false;

void signal_handler(int);
void exit_handler();

int main(int argc, char **argv) {

	auto& msr = config::msr;

	int msg_fd;

	config::init(argc, argv);

	atexit(exit_handler);

	signal(SIGINT, signal_handler);

	if(not config::parse()) {
		config::usage();
		return EXIT_FAILURE;
	}

	if(config::device == nullptr) {
		std::cerr << "msr device filename not specified (use -d flag)" << std::endl;
		return EXIT_FAILURE;
	}

	if(config::verbose) {
		msg_fd = STDOUT_FILENO;
	} else {
		msg_fd = open("/dev/null", O_WRONLY);
		if(msg_fd == -1) {
			perror("open(\"/dev/null\", ...)");
			return EXIT_FAILURE;
		}
	}

	if(config::verbose) {

		std::cout << "verbose=" << ( config::verbose ? "ENABLED" : "DISABLED" ) << std::endl;
		std::cout << "test="    << ( config::test    ? "ENABLED" : "DISABLED" ) << std::endl;
		std::cout << "info="    << ( config::info    ? "ENABLED" : "DISABLED" ) << std::endl;
		std::cout << "cli="     << ( config::cli     ? "ENABLED" : "DISABLED" ) << std::endl;

		std::cout << "glob="   << config::glob   << std::endl;
		std::cout << "device=" << config::device << std::endl;
	}

	if(config::verbose)
		std::cout << "[START]" << std::endl;

	if(msr.start(config::device, STDIN_FILENO, msg_fd) == false) {
		std::cerr << "failed to start device " << config::device << ": " << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	if(not msr.reset()) {
		perror("RESET");
		return EXIT_FAILURE;
	}

	if(config::info) {

		char model = msr.model();
		std::string firmware = msr.firmware();

		std::cout << "/info-mode/" << std::endl;

		if(model == '\0') {
			perror("MODEL");
			return EXIT_FAILURE;
		}

		if(firmware.empty()) {
			perror("FIRMWARE");
			return EXIT_FAILURE;
		}

		std::cout << "model=" << model << std::endl;
		std::cout << "firmware=" << firmware << std::endl;
		std::cout << "tracks=";

		if(msr.has_track1()) std::cout << '1';
		if(msr.has_track2()) std::cout << '2';
		if(msr.has_track3()) std::cout << '3';

		std::cout << std::endl;
	}

	if(config::test) {

		std::cout << "/test-mode/" << std::endl;

		std::cout << "; slide card for sensor test" << std::endl;

		std::cout << "sensor-test: " << (msr.test_sensor() ? "PASS" : "FAIL") << std::endl;
		std::cout << "comm-test: "   << (msr.test_comm  () ? "PASS" : "FAIL") << std::endl;
		std::cout << "RAM-test: "    << (msr.test_ram   () ? "PASS" : "FAIL") << std::endl;
	}

	if(config::cli) {

		std::string prompt = msr.firmware() + "> ";

		char *line;

		std::cout << "/cli-mode/" << std::endl;

		while(not done and (line = readline(prompt.c_str())) != nullptr) {

			if(strcasecmp(line, "ERASE") == 0) {

				std::cout << "/batch-erase/ ; press enter to stop" << std::endl;

				while(msr.erase())
					msleep(250);

				perror("ERASE");

			} else if(strcasecmp(line, "RESET") == 0) {

				msr.reset();

			} else if(strcasecmp(line, "QUIT") == 0) {

				done = true;
			}

			free(line);
		}
	}

	return EXIT_SUCCESS;
}

void exit_handler() {
	putchar('\n');
	if(config::verbose)
		std::cout << "[QUIT]" << std::endl;
}

void signal_handler(int signo) {
	if(signo == SIGINT) {
		signal(signo, SIG_IGN);
		exit(EXIT_FAILURE);
	}
}
