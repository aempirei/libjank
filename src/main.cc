#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>

#include <cstring>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <signal.h>

#include <jank.hh>

void signal_handler(int);
void exit_handler();

namespace config {

		bool verbose = false;
		bool info = false;
		bool test = false;

		const char *glob = "/dev/ttyUSB*";
		const char *device = "/dev/ttyUSB0";

		int argc;
		char **argv;

		jank::msr msr;

		void usage() {

				std::string prog = basename(argv[0]);

				std::cout << std::endl << "usage: " << prog << " [options] -d device" << std::endl << std::endl;

				std::cout << "\t-h          show this help" << std::endl;
				std::cout << "\t-t          toggle test mode (default=" << (test ? "ENABLED" : "DISABLED") << ")" << std::endl;
				std::cout << "\t-v          toggle verbose mode (default=" << (verbose ? "ENABLED" : "DISABLED") << ")" << std::endl;
				std::cout << "\t-i          toggle info mode (default=" << (info ? "ENABLED" : "DISABLED") << ")" << std::endl;

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

				while((opt = getopt(argc, argv, "hvitd:")) != -1) {

						switch(opt) {

								case 'v': verbose = not verbose ; break;
								case 'i': info    = not info    ; break;
								case 't': test    = not test    ; break;

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

void exit_handler() {
		config::msr.off();
		if(config::verbose)
				std::cout << "[STOP]" << std::endl;
		config::msr.stop();
}

void signal_handler(int signo) {
		if(signo == SIGINT) {
				signal(signo, SIG_IGN);
				exit(EXIT_FAILURE);
		}
}

int main(int argc, char **argv) {

		auto& msr = config::msr;

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
				std::cout << "verbose=" << ( config::verbose ? "ENABLED" : "DISABLED" ) << std::endl;
				std::cout << "test=" << ( config::test ? "ENABLED" : "DISABLED" ) << std::endl;
				std::cout << "info=" << ( config::info ? "ENABLED" : "DISABLED" ) << std::endl;
				std::cout << "glob=" << config::glob << std::endl;
				std::cout << "device=" << config::device << std::endl;
		}

		if(config::verbose)
				std::cout << "[START]" << std::endl;

		if(msr.start(config::device) == false) {
				std::cerr << "failed to start device " << config::device << std::endl;
				return EXIT_FAILURE;
		}

		if(config::verbose)
				std::cout << "[RESET]" << std::endl;

		if(not msr.reset  ()) perror("RESET command failed");
		if(not msr.green  ()) perror("Green LED On failed" ); msleep(100);
		if(not msr.yellow ()) perror("Yellow LED On failed"); msleep(100);
		if(not msr.red    ()) perror("Red LED On failed"   ); msleep(100);
		if(not msr.on     ()) perror("All LEDs On failed"  ); msleep(100);
		if(not msr.off    ()) perror("All LEDs Off failed" ); msleep(100);
 
		if(config::test) {

				std::cout << "/test-mode/" << std::endl;

				std::cout << "comm-test: "   << (msr.test_comm  () ? "PASS" : "FAIL") << std::endl;
				std::cout << "RAM-test: "    << (msr.test_ram   () ? "PASS" : "FAIL") << std::endl;
				std::cout << "sensor-test: " << (msr.test_sensor() ? "PASS" : "FAIL") << std::endl;
		}

		if(config::info) {

				int model = msr.model();
				std::string firmware = msr.firmware();

				std::cout << "/info-mode/" << std::endl;

				if(model == '\0') {
						perror("msr::model()");
						return EXIT_FAILURE;
				}

				if(firmware.empty()) {
						perror("msr::firmware()");
						return EXIT_FAILURE;
				}

				std::cout << "model-info=" << model << std::endl;
				std::cout << "firmware-info=" << firmware << std::endl;
		}

		return EXIT_SUCCESS;
}
