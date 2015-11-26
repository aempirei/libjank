#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include <cstring>

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include <jank.hh>

namespace jank {

		msr::msr() : active(false) {
		}

		msr::~msr() {
				if(active) {
						reset();
						stop();
				}
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

		bool msr:: reset() const { return writen(ESC "a", 2) == 2; }

		bool msr::   red() const { return writen(ESC "\x85", 2) == 2; }
		bool msr::yellow() const { return writen(ESC "\x84", 2) == 2; }
		bool msr:: green() const { return writen(ESC "\x83", 2) == 2; }
		bool msr::    on() const { return writen(ESC "\x82", 2) == 2; }
		bool msr::   off() const { return writen(ESC "\x81", 2) == 2; }

		bool msr::test_comm() const {
				return expect(ESC "e", 2, ESC "y", 2);
		}

		bool msr::test_ram() const {
				return expect(ESC "\x87", 2, ESC "0", 2);
		}

		bool msr::test_sensor() const {
				return expect(ESC "\x86", 2, ESC "0", 2);
		}

		bool msr::has_track1() const {
				char m = model();
				return m == '3' or m == '5';
		}

		bool msr::has_track2() const {
				return true;
		}

		bool msr::has_track3() const {
				char m = model();
				return m == '2' or m == '3';
		}

		char msr::model() const {

				constexpr size_t buf_sz = 3;
				char buf[buf_sz];
				ssize_t n;

				if(writen(ESC "t", 2) != 2)
						return 0;

				n = readn(buf, buf_sz);
				if(n == -1)
						return 0;

				if(buf[0] != '\033' or buf[2] != 'S') {
						errno = ENOMEDIUM;
						return 0;
				}

				return buf[1];
		}

		std::string msr::firmware() const {

				constexpr size_t buf_sz = 9;
				char buf[buf_sz];
				ssize_t n;

				if(writen(ESC "v", 2) != 2)
						return "";

				n = readn(buf, buf_sz);
				if(n == -1)
						return "";

				return std::string(buf + 1, buf_sz - 1);
		}

		bool msr::expect(const void *tx, size_t tx_sz, const void *rx, size_t rx_sz) const {

				char *buf;

				ssize_t n;

				if(writen(tx, tx_sz) != (ssize_t)tx_sz)
						return false;

				buf = new char[rx_sz];

				n = readn(buf, rx_sz);

				if(n == -1) {
						delete[] buf;
						return false;
				}

				bool X = memncmp(buf, n, rx, rx_sz) == 0;

				delete[] buf;

				return X;
		}

		int msr::memncmp(const void *s1, size_t s1_sz, const void *s2, size_t s2_sz) const {

			int n = memcmp(s1, s2, std::min(s1_sz, s2_sz));

			return n ? n : (s1_sz - s2_sz);
		}

		ssize_t msr::writen(const void *buf, size_t sz) const {

				const char *p = (const char *)buf;

				size_t left = sz;
				size_t done = 0;
				ssize_t n;

				if(not active) {
						errno = ENOMEDIUM;
						return -1;
				}

				while(left > 0) {

						n = write(fd, p + done, left);

						if(n == -1) {
								if(errno == EINTR)
										continue;
								return -1;
						}

						left -= n;
						done += n;
				}

				return done;
		}

		ssize_t msr::readn(void *buf, size_t sz) const {

				char *p = (char *)buf;

				size_t left = sz;
				size_t done = 0;
				ssize_t n;

				if(not active) {
						errno = ENOMEDIUM;
						return -1;
				}

				while(left > 0) {

						n = read(fd, p + done, left);

						if(n == -1) {
								if(errno == EINTR)
										continue;
								return -1;
						}

						left -= n;
						done += n;
				}

				return done;
		}

		std::string msr::hex(const char *s, size_t sz) const {

				std::stringstream ss;

				ss << std::dec << "char s[" << sz << "] = {";

				for(unsigned int n = 0; n < sz; n++)
						ss << ' ' << std::hex << std::setfill('0') << std::setw(2) << (int)s[n];

				ss << " };";

				return ss.str();
		}
}
