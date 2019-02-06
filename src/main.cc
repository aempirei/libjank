#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>
#include <regex>

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

	bool verbose = false;
	bool info = false;
	bool test = false;
	bool cli = false;
	bool detect = false;
	bool led = false;

	const char *glob = "/dev/ttyUSB*";
	const char *device = "/dev/ttyUSB0";
	// const char *device = "/dev/msr605";

	int argc;
	char **argv;

	jank::msr msr;

	void usage() {

		std::string prog = basename(argv[0]);

		std::cout << std::endl << "usage: " << prog << " [options] -d device" << std::endl << std::endl;

		std::cout << "\t-h          show this help" << std::endl;

		std::cout << "\t-t          toggle test mode (default="                     << (test    ? "ENABLED" : "DISABLED") << ")" << std::endl;
		std::cout << "\t-v          toggle verbose mode (default="                  << (verbose ? "ENABLED" : "DISABLED") << ")" << std::endl;
		std::cout << "\t-i          toggle info mode (default="                     << (info    ? "ENABLED" : "DISABLED") << ")" << std::endl;
		std::cout << "\t-c          toggle command-line mode (default="             << (cli     ? "ENABLED" : "DISABLED") << ")" << std::endl;
		std::cout << "\t-D          toggle MSR-605 device detection mode (default=" << (detect  ? "ENABLED" : "DISABLED") << ")" << std::endl;
		std::cout << "\t-L          toggle LED flashing mode (default="             << (led     ? "ENABLED" : "DISABLED") << ")" << std::endl;

		std::cout << "\t-d device   filename of MSR-605 device";

		if(device != nullptr)
			std::cout << " (default=" << device << ")";

		std::cout << std::endl;

		std::cout << std::endl;

	}

	void init(int my_argc, char **my_argv) {

		argc = my_argc;
		argv = my_argv;
	}

	bool parse() {

		int opt;

		while((opt = getopt(argc, argv, "hvitcDLd:")) != -1) {

			switch(opt) {

				case 'v': verbose = not verbose ; break;
				case 'i': info    = not info    ; break;
				case 't': test    = not test    ; break;
				case 'c': cli     = not cli     ; break;
				case 'D': detect  = not detect  ; break;
				case 'L': led     = not led     ; break;

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
void flash(const jank::msr&, int, int);
void print_track(unsigned int, const std::string&);

int main(int argc, char **argv) {

	auto& msr = config::msr;

	int msg_fd;
	int oob_fd;

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
			perror("open(\"/dev/null\", O_WRONLY)");
			return EXIT_FAILURE;
		}
	}

	if(config::cli) {
		oob_fd = STDIN_FILENO;
	} else {
		oob_fd = open("/dev/null", O_RDONLY);
		if(oob_fd == -1) {
			perror("open(\"/dev/null\", O_RDONLY)");
			return EXIT_FAILURE;
		}
	}

	if(config::verbose) {

		std::cout << "verbose=" << ( config::verbose ? "ENABLED" : "DISABLED" ) << std::endl;
		std::cout << "detect="  << ( config::detect  ? "ENABLED" : "DISABLED" ) << std::endl;
		std::cout << "test="    << ( config::test    ? "ENABLED" : "DISABLED" ) << std::endl;
		std::cout << "info="    << ( config::info    ? "ENABLED" : "DISABLED" ) << std::endl;
		std::cout << "cli="     << ( config::cli     ? "ENABLED" : "DISABLED" ) << std::endl;

		std::cout << "glob="   << config::glob   << std::endl;
		std::cout << "device=" << config::device << std::endl;
	}

	if(config::verbose)
		std::cout << "[START]" << std::endl;

	if(msr.start(config::device, oob_fd, msg_fd) == false) {
		std::cerr << "failed to start device " << config::device << ": " << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	if(not msr.reset()) {
		perror("RESET");
		return EXIT_FAILURE;
	}

	if(config::detect) {
		msr.sync_timeout = 1;
		return msr.model() == '\0' ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	if(config::led) {
		flash(msr, 4, 100);
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

		char prompt[64];
		char *line;

		snprintf(prompt, sizeof(prompt), "%s> ", msr.firmware());

		std::cout << "/cli-mode/" << std::endl;

		while(not done and (line = readline(prompt)) != nullptr) {

			if(strncasecmp(line, "ERASE", 5) == 0) {

				int t1, t2, t3;

				if(sscanf(line, " %*s %d %d %d ", &t1, &t2, &t3) == 3) {
					// do nothing
				} else {
					t1 = t2 = t3 = true;
				}

				int n = 0;

				std::cout << "/batch-erase-";
				if(t1) std::cout << '1';
				if(t2) std::cout << '2';
				if(t3) std::cout << '3';
				std::cout << '/' << std::endl;

				msr.flush();

				do {
					std::cout << "[" << ++n << "] swipe card or press <ENTER> to stop." << std::endl;
					flash(msr, 3, 50);
				} while(msr.erase(t1,t2,t3));

				perror("ERASE");

			} else if(strncasecmp(line, "WRITE", 5) == 0) {
				char tr[3][128] = { "", "", "" };
				if(
						(sscanf(line, " %*s \"%[^\"]\" \"%[^\"]\" \"%[^\"]\" ", tr[0], tr[1], tr[2]) == 3) ||
						(sscanf(line, " %*s \"%[^\"]\" \"%[^\"]\" - ", tr[0], tr[1]) == 2) ||
						(sscanf(line, " %*s \"%[^\"]\" - \"%[^\"]\" ", tr[0], tr[2]) == 2) ||
						(sscanf(line, " %*s - \"%[^\"]\" \"%[^\"]\" ", tr[1], tr[2]) == 2) ||
						(sscanf(line, " %*s \"%[^\"]\" - - ", tr[0]) == 1) ||
						(sscanf(line, " %*s - \"%[^\"]\" - ", tr[1]) == 1) ||
						(sscanf(line, " %*s - - \"%[^\"]\" ", tr[2]) == 1)
				  ) {
					for(unsigned int no = 1; no <= 3; no++)
						print_track(no, tr[no-1]);
					std::cout << "swipe write card or press <ENTER> to cancel." << std::endl;

					msleep(500);

					if(!msr.write(tr[0], tr[1], tr[2]))
						perror("WRITE");
				} else
					perror("WRITE");
			} else if(strcasecmp(line, "COPY") == 0) {

				bool done = false;

				std::string track1;
				std::string track2;
				std::string track3;

				std::cout << "/copy/" << std::endl;

				while(not done) {

					std::cout << "swipe read card or press <ENTER> to cancel." << std::endl;

					if(msr.read(track1, track2, track3)) {

						print_track(1, track1);
						print_track(2, track2);
						print_track(3, track3);

						if(not track1.empty() and track1.front() == '%' and track1.back() == '?')
							track1 = track1.substr(1, std::string::npos);

						msleep(500);

						while(not done) {

							std::cout << "swipe write card or press <ENTER> to cancel." << std::endl;

							if(msr.write(track1, track2, track3)) {

								done = true;

							} else {

								perror("WRITE");

								if(errno == ECANCELED or errno == EINVAL)
									break;
							}

							msleep(500);
						}

					} else {

						perror("READ");

						if(errno == ECANCELED)
							break;
					}

					msleep(500);
				}

			} else if(strncasecmp(line, "TRACK2", 6) == 0) {
				int n = 0;
				char fn[256];
				if(sscanf(line, "%*s %255s ", fn) == 1) {
					FILE *f = fopen(fn, "r");
					if(f == NULL) {
						perror("fopen()");
					} else {
						bool cancel = false;
						char fileline[256];
						std::cout << "/batch-write-track2/" << std::endl;
						while(not cancel and fgets(fileline, sizeof(fileline) - 1, f) != NULL) {

							std::string s(fileline);
							std::smatch m;
							std::regex e ("\\b\\d{15,19}=\\d{4,60}\\b");

							while (not cancel and std::regex_search (s,m,e)) {
								auto track2 = m.str();

								std::cout << "[" << ++n << "] track2 = " << track2 << std::endl;
								std::cout << "[" << n << "] swipe card or press <ENTER> to stop." << std::endl;
								while(!msr.write("", track2, "")) {
									auto en = errno;
									perror("WRITE");
									msr.flush();
									errno = en;
									if(errno != ENOMEDIUM or errno == ECANCELED) { 
										cancel = true;
										break;
									}
									std::cout << "[" << n << "] retry, swipe card or press <ENTER> to stop." << std::endl;
									msleep(500);
								}

								msleep(500);
								s = m.suffix().str();
							}
						}
					}
				}
			} else if(strcasecmp(line, "READ") == 0) {

				int n = 0;

				std::string track1;
				std::string track2;
				std::string track3;

				std::cout << "/batch-read/" << std::endl;

				for(;;) {

					std::cout << '[' << (++n) << "] swipe card or press <ENTER> to stop." << std::endl;

					if(!msr.read(track1, track2, track3)) {

						perror("READ");

						if(errno == ECANCELED)
							break;
					}

					print_track(1, track1);
					print_track(2, track2);
					print_track(3, track3);

					msleep(500);
				}
			} else if(strcasecmp(line, "HICO") == 0) {
				msr.set_hico();
			} else if(strcasecmp(line, "LOCO") == 0) {
				msr.set_loco();
			} else if(strcasecmp(line, "CO?") == 0) {
				std::cout << "/get-coercivity/" << std::endl;
				if(msr.is_hico()) std::cout << "HICO" << std::endl;
				if(msr.is_loco()) std::cout << "LOCO" << std::endl;
			} else if(strcasecmp(line, "RED") == 0) {
				msr.red();
			} else if(strcasecmp(line, "YELLOW") == 0) {
				msr.yellow();
			} else if(strcasecmp(line, "GREEN") == 0) {
				msr.green();
			} else if(strcasecmp(line, "ON") == 0) {
				msr.on();
			} else if(strcasecmp(line, "OFF") == 0) {
				msr.off();
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

void print_track(unsigned int no, const std::string& track) {
	std::cout << "track" << no << " (" << jank::track::status(track) << ')';
	if(jank::track::is_ok(track))
		std::cout << ' ' << track;
	std::cout << std::endl;
}

void flash(const jank::msr& msr, int n, int ms) {
	for(int y = 0; y < n; y++) {
		msleep(ms);
		msr.on();
		msleep(ms);
		msr.off();
	}
	msleep(ms);
}

void exit_handler() {
}

void signal_handler(int signo) {
	if(signo == SIGINT) {
		signal(signo, SIG_IGN);
		putchar('\n');
		exit(EXIT_FAILURE);
	}
}
