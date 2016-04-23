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

#define ESC "\33"

#define compare_position(I,B,R)	\
								\
	if((I)==(B).end())			\
		continue;				\
	if(not(R(*(I)))) {			\
		errno = EPROTO;			\
		break;					\
	}							\
	(I)++;

#define is(c)		(c) ==
#define isescape(c) ((c) == '\33')
#define isstatus(c) ((c) >= '0' and (c) <= '?')

namespace jank {

	const msr::pattern_type<2> msr::response_ok = { { '\33', '0' } };
	const msr::pattern_type<2> msr::response_fail = { { '\33', 'A' } };
	const msr::pattern_type<2> msr::response_ack = { { '\33', 'y' } };

	msr::msr() : active(false), sync_timeout(30) {
			memset(&cache, 0, sizeof(cache));
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

		n = ::read(fd, read_block, sizeof(read_block));
		if(n == -1)
			return errno == EINTR;

		for(ssize_t i = 0; i < n; i++)
			buffer.push_back(read_block[i]);

		return true;
	}

	void msr::message(const char *msg) const {
		::write(msg_fd, "[", 1);
		::write(msg_fd, msg, strlen(msg));
		::write(msg_fd, "]\n", 2);
	}

	bool msr::stop() {

		if(not active) {
			errno = ENOMEDIUM;
			return false;
		}

		message("STOP");

		if(cache.firmware) {
			delete[] cache.firmware;
			cache.firmware = nullptr;
		}

		if(cache.model) {
			delete[] cache.model;
			cache.model = nullptr;
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
		message("RESET");
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

	template <class T, class U> std::pair<bool,typename T::const_iterator> msr::begins_with(const T& a, const U& b) const {

		auto iter = a.cbegin();
		auto jter = b.cbegin();

		while(iter != a.end() and jter != b.end() and *iter++ == *jter++)
			void();

		return std::make_pair(jter == b.end(), iter);
	}

	bool msr::erase() {
		return erase(true,true,true);
	}

	bool msr::cancel() {
			if(not oob_buffer.empty()) {
				if(oob_buffer.back() == '\n') {
					errno = ECANCELED;
					return true;
				}
			}

			return false;
	}

	bool msr::erase(bool t1, bool t2, bool t3) {

		const char tracks = (t1 ? 1 : 0) | (t2 ? 2 : 0) | (t3 ? 4 : 0);
		const char cmd[] = { '\33', 'c', tracks == 1 ? '\0' : tracks };

		message("ERASE");

		if(writen(cmd, sizeof(cmd)) != sizeof(cmd))
				return false;

		while(sync() and not cancel()) {

				auto response = begins_with(msr_buffer, response_ok);

				if(response.first) {
						msr_buffer.erase(msr_buffer.cbegin(), response.second);
						return true;
				}

				response = begins_with(msr_buffer, response_fail);

				if(response.first) {
						msr_buffer.erase(msr_buffer.cbegin(), response.second);
						errno = EIO;
						return false;
				}
		}

		int e = errno;

		msleep(250);

		if(reset() and flush())
			errno = e;

		return false;
	}

	bool msr::write(const std::string& track1, const std::string& track2, const std::string& track3) {

			message("WRITE");

			std::stringstream ss;

			ss << "\33w\33s\33\1" << track1 << "\33\2" << track2 << "\33\3" << track3 << "?\34";

			std::string cmd = ss.str();

			if(writen(cmd.c_str(), cmd.length()) != (ssize_t)cmd.length())
					return false;

			while(sync() and not cancel()) {

					auto iter = msr_buffer.cbegin();

					if(iter == msr_buffer.end()) continue; if(*iter++ != '\33') { errno = EPROTO; break; }
					if(iter == msr_buffer.end()) continue; if(*iter < '0' or *iter > '?') { errno = EPROTO; break; }

					char status = *iter++;

					msr_buffer.erase(msr_buffer.cbegin(), iter);

					switch(status) {
							case '0': return true;
							case '1': errno = EIO; break;
							case '2': errno = EINVAL; break;
							case '4': errno = ENOTSUP; break;
							case '9': errno = ENOMEDIUM; break;
					}

					break;
			}

			int e = errno;

			msleep(250);

			if(reset() and flush())
					errno = e;

			return false;

	}

	bool msr::read(std::string& track1, std::string& track2, std::string& track3) {

		const char cmd[] = { '\33', 'r' };

		message("READ");

		if(writen(cmd, sizeof(cmd)) != sizeof(cmd))
			return false;

		while(sync() and not cancel()) {

			for(auto ch : msr_buffer) {
				if(isprint(ch))
					std::cout << (char)ch;
				else
					std::cout << "\\x" << std::hex << std::setfill('0') << std::setw(2) << (int)ch;
			}

			auto iter = msr_buffer.cbegin();

			compare_position(iter, msr_buffer, isescape);
			compare_position(iter, msr_buffer, is('s'));
			compare_position(iter, msr_buffer, isescape);
			compare_position(iter, msr_buffer, is('\1'));

			track1.clear();

			while(iter != msr_buffer.end() and *iter != '\33')
					track1.push_back(*iter++);

			compare_position(iter, msr_buffer, isescape);
			compare_position(iter, msr_buffer, is('\2'));

			track2.clear();

			while(iter != msr_buffer.end() and *iter >= '0' and *iter <= '?')
					track2.push_back(*iter++);

			compare_position(iter, msr_buffer, isescape);
			compare_position(iter, msr_buffer, is('\3'));

			track3.clear();

			while(iter != msr_buffer.end() and *iter != '\34')
					track3.push_back(*iter++);

			compare_position(iter, msr_buffer, is('\34'));
			compare_position(iter, msr_buffer, isescape);
			compare_position(iter, msr_buffer, isstatus);

			char status = *std::prev(iter);

			msr_buffer.erase(msr_buffer.cbegin(), iter);

			switch(status) {
					case '0': return true;
					case '1': errno = EIO; break;
					case '2': errno = EINVAL; break;
					case '4': errno = ENOTSUP; break;
			}

			break;
		}

		int e = errno;

		msleep(250);

		if(reset() and flush())
			errno = e;

		return false;
	}

	bool msr::has_track1() {
		char m = model();
		return m == '3' or m == '5';
	}

	bool msr::has_track2() {
		char m = model();
		return m != -1;
	}

	bool msr::has_track3() {
		char m = model();
		return m == '2' or m == '3';
	}

	char msr::model() {

			const char cmd[] = { '\33', 't' };

			message("MODEL");

			if(cache.model != nullptr)
					return *cache.model;

			if(writen(cmd, sizeof(cmd)) != sizeof(cmd))
					return '\0';

			while(sync() and not cancel()) {

					auto iter = msr_buffer.cbegin();

					char ch;

					if(iter == msr_buffer.end()) continue; if(*iter != '\33') { errno = EPROTO; break; } else iter++;
					if(iter == msr_buffer.end()) continue; if(not isdigit(*iter)) { errno = EPROTO; break; } else ch = *iter++;
					if(iter == msr_buffer.end()) continue; if(*iter != 'S') { errno = EPROTO; break; } else iter++;

					msr_buffer.erase(msr_buffer.cbegin(), iter);

					cache.model = new char;

					*cache.model = ch;

					return *cache.model;
			}

			int e = errno;

			msleep(250);

			if(reset() and flush())
					errno = e;

			return '\0';
	}

	const char *msr::firmware() {

			const char cmd[] = { '\33', 'v' };

			message("FIRMWARE");

			if(cache.firmware != nullptr)
					return cache.firmware;

			if(writen(cmd, sizeof(cmd)) != sizeof(cmd))
					return nullptr;

			while(sync() and not cancel()) {

					auto iter = msr_buffer.cbegin();

					std::string s;

					if(iter == msr_buffer.end()) continue; if(*iter != '\33') { errno = EPROTO; break; } else iter++;
					if(iter == msr_buffer.end()) continue; if(*iter != 'R') { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(*iter != 'E') { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(*iter != 'V') { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(not isalpha(*iter)) { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(not isdigit(*iter)) { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(*iter != '.') { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(not isdigit(*iter)) { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(not isdigit(*iter)) { errno = EPROTO; break; } else s.push_back(*iter++);

					msr_buffer.erase(msr_buffer.cbegin(), iter);

					cache.firmware = new char[s.length() + 1];

					strcpy(cache.firmware, s.c_str());

					return cache.firmware;
			}

			int e = errno;

			msleep(250);

			if(reset() and flush())
					errno = e;

			return nullptr;
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

			n = ::write(msr_fd, p + done, left);

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

			n = ::read(msr_fd, p + done, left);

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

		ss << "char s[" << std::dec << sz << "] = {";

		for(unsigned int n = 0; n < sz; n++)
			ss << ' ' << std::hex << std::setfill('0') << std::setw(2) << (int)s[n];

		ss << " };";

		return ss.str();
	}
}
