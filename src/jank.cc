#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include <cstring>

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

#include <jank.hh>

#define ESC "\033"

namespace jank {

	const msr::pattern_type<2> msr::response_ok = { { '\033', '0' } };
	const msr::pattern_type<2> msr::response_fail = { { '\033', 'A' } };
	const msr::pattern_type<2> msr::response_ack = { { '\033', 'y' } };

	msr::msr() : active(false), sync_timeout(5) {
	}

	msr::~msr() {
		if(active) {
			reset();
			stop();
		}
	}

	bool msr::start(const char *my_device, int my_oob_fd, int my_msg_fd) {

		termios options;

		if(active) {
			errno = EALREADY;
			return false;
		}

		device = my_device;

		oob_fd = my_oob_fd;
		msg_fd = my_msg_fd;

		msr_fd = open(device.c_str(), O_RDWR | O_NOCTTY);
		if(msr_fd == -1)
			return false;

		tcgetattr(msr_fd, &options);

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

		if(tcsetattr(msr_fd, TCSANOW, &options) == -1) {
			int e = errno;
			close(msr_fd);
			errno = e;
			return false;
		}

		active = true;

		return true;
	}

	bool msr::sync() {

		struct timeval tv = { sync_timeout, 0 };

		int n;

		fd_set rfds;

		FD_ZERO(&rfds);

		FD_SET(msr_fd, &rfds);
		FD_SET(oob_fd, &rfds);

		n = select(std::max(msr_fd, oob_fd) + 1, &rfds, nullptr, nullptr, &tv);
		if(n == -1)
			return errno == EINTR;

		if(n == 0) {
			errno = ETIME;
			return false;
		}

		if(FD_ISSET(msr_fd, &rfds))
			if(not update(msr_fd, msr_buffer))
				return false;

		if(FD_ISSET(oob_fd, &rfds)) 
			if(not update(oob_fd, oob_buffer))
				return false;

		return true;
	}

	bool msr::update(int fd, buffer_type& buffer) {

		char read_block[read_block_sz];

		ssize_t n;

		n = read(fd, read_block, sizeof(read_block));
		if(n == -1)
			return errno == EINTR;

		for(ssize_t i = 0; i < n; i++)
			buffer.push_back(read_block[i]);

		return true;
	}

	bool msr::stop() {

		if(not active) {
			errno = ENOMEDIUM;
			return false;
		}

		close(msr_fd);
		active = false;

		return true;
	}

	bool msr::flush() {

		oob_buffer.clear();
		msr_buffer.clear();

		if(tcflush(oob_fd, TCIFLUSH) == -1)
			return false;

		if(tcflush(msr_fd, TCIOFLUSH) == -1)
			return false;

		return true;
	}

	bool msr::reset() const {
		const char msg[] = "[RESET]\n";
		write(msg_fd, msg, strlen(msg));
		return writen(ESC "a", 2) == 2;
	}

	bool msr::red() const {
		return writen(ESC "\x85", 2) == 2;
	}

	bool msr::yellow() const {
		return writen(ESC "\x84", 2) == 2;
	}

	bool msr::green() const {
		return writen(ESC "\x83", 2) == 2;
	}

	bool msr::on() const {
		return writen(ESC "\x82", 2) == 2;
	}

	bool msr::off() const {
		return writen(ESC "\x81", 2) == 2;
	}

	bool msr::test_comm() const {
		return expect(ESC "e", 2, ESC "y", 2);
	}

	bool msr::test_ram() const {
		return expect(ESC "\x87", 2, ESC "0", 2);
	}

	bool msr::test_sensor() const {
		return expect(ESC "\x86", 2, ESC "0", 2);
	}

	template <class T, class U> bool msr::begins_with(const T& a, const U& b) const {

		auto iter = a.cbegin();
		auto jter = b.cbegin();

		while(iter != a.end() and jter != b.end() and *iter++ == *jter++)
			void();

		return jter == b.end();
	}

	bool msr::erase() {
		return erase(true,true,true);
	}

	bool msr::erase(bool t1, bool t2, bool t3) {

		const char tracks = (t1 ? 1 : 0) | (t2 ? 2 : 0) | (t3 ? 4 : 0);
		const char cmd[] = { '\033', 'c', tracks == 1 ? '\0' : tracks };
		const char msg[] = "[ERASE]\n";

		write(msg_fd, msg, strlen(msg));

		if(writen(cmd, sizeof(cmd)) != sizeof(cmd))
			return false;

		while(sync()) {

			if(not oob_buffer.empty()) {
				if(oob_buffer.back() == '\n') {
					if(reset() and flush())
						errno = ECANCELED;
					return false;
				}
			}

			if(begins_with(msr_buffer, response_ok)) {
				for(size_t i = 0; i < response_ok.size(); i++)
					msr_buffer.pop_front();
				return true;
			}

			if(begins_with(msr_buffer, response_fail))
				for(size_t i = 0; i < response_fail.size(); i++)
					msr_buffer.pop_front();
				errno = EREMOTE; 
				return false;
		}

		return false;
	}

	bool msr::has_track1() const {
		char m = model();
		return m == '3' or m == '5';
	}

	bool msr::has_track2() const {
		char m = model();
		return m != -1;
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
			return -1;

		n = readn(buf, buf_sz);
		if(n == -1)
			return -1;

		if(buf[0] != '\033' or buf[2] != 'S') {
			errno = EPROTO;
			return -1;
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

			n = write(msr_fd, p + done, left);

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

			n = read(msr_fd, p + done, left);

			if(n == -1) {
				if(errno == EINTR)
					continue;
				return -1;
			}

			left -= n;
			done += n;
		}

		// std::cout << hex((char *)buf, done) << std::endl;

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
