#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <regex>

#include <cstring>
#include <cctype>

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

#include <jank.hh>

#define ESC "\033"

#define compare_position(I,B,R)	\
				\
	if((I)==(B).end())	\
		continue;	\
	if(not(R(*(I)))) {	\
		errno = EPROTO;	\
		break;		\
	}			\
	(I)++

#define compare_position_nf(I,B,R)	\
					\
	if((I)==(B).end())		\
		continue;		\
	if(not(R(*(I))))		\
		continue;		\
	(I)++

#define is(L)		(L) ==
#define isescape(R)	(is('\033')(R))
#define isstatus(R)	((R) >= '0' and (R) <= '?')

namespace jank {

	const pattern_type<2> response::ok = { { '\033', '0' } };
	const pattern_type<2> response::fail = { { '\033', 'A' } };
	const pattern_type<2> response::ack = { { '\033', 'y' } };

	const std::string track::empty = "\033+";
	const std::string track::error = "\033*";

	std::string track::status(const std::string& s) {
		return s == empty ? "EMPTY" : s == error ? "ERROR" : "OK";
	}

	bool track::is_ok(const std::string& s) {
		return s != empty && s != error;
	}

	template <class T, class U> std::pair<bool,typename T::iterator> begins_with(T& a, const U& b) {

		auto iter = a.begin();
		auto jter = b.cbegin();

		while(iter != a.end() and jter != b.end() and *iter++ == *jter++)
			void();

		return std::make_pair(jter == b.cend(), iter);
	}

	msr::msr() : active(false), sync_timeout(30), msr_errno(0) {
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
			goto failure;

		tcgetattr(msr_fd, &options);

		if(cfsetispeed(&options, 0) == -1)
			goto failure;
		if(cfsetospeed(&options, B9600) == -1)
			goto failure;

		options.c_cflag |= (CLOCAL | CREAD);

		options.c_cflag &= ~PARENB;
		options.c_cflag &= ~CSTOPB;
		options.c_cflag &= ~CSIZE;
		options.c_cflag |= CS8;

		options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

		options.c_cc[VMIN]  = 1;
		options.c_cc[VTIME] = 0;

		options.c_oflag &= ~OPOST;

		if(tcsetattr(msr_fd, TCSANOW, &options) == -1)
			goto failure;

		active = true;

		return true;

	failure:
		if(msr_fd != -1) {
			int e = errno;
			close(msr_fd);
			errno = e;
		}
		return false;
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

	bool msr::set_hico() {
		return expect(ESC "x", 2, ESC "0", 2);
	}

	bool msr::set_loco() {
		return expect(ESC "y", 2, ESC "0", 2);
	}

	bool msr::is_hico() {
		return expect(ESC "d", 2, ESC "h", 2);
	}

	bool msr::is_loco() {
		return expect(ESC "d", 2, ESC "l", 2);
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
		const char cmd[] = { '\033', 'c', tracks == 1 ? '\0' : tracks };

		message("ERASE");

		if(writen(cmd, sizeof(cmd)) != sizeof(cmd))
				return false;

		while(sync() and not cancel()) {

				auto resp = begins_with(msr_buffer, response::ok);

				if(resp.first) {
						msr_buffer.erase(msr_buffer.begin(), resp.second);
						return true;
				}

				resp = begins_with(msr_buffer, response::fail);

				if(resp.first) {
						msr_buffer.erase(msr_buffer.begin(), resp.second);
						errno = EIO;
						return false;
				}
		}

		int e = errno;

		msleep(250);

		reset();
		flush();

		errno = e;

		return false;
	}

	std::string msr::msr_strerror(int errnum) {
		switch(errnum) {
			case 0: return "command ok";
			case 1: return "read/write error";
			case 2: return "command format error";
			case 4: return "invalid command";
			case 9: return "invalid card swipe";
		}
		return "unknown error";
	}

	bool msr::write(const std::string& track1, const std::string& track2, const std::string& track3) {

			message("WRITE");

			std::string t1(track1);
			std::string t2(track2);
			std::string t3(track3);

			if(t1 == jank::track::empty) t1.clear();
			if(t2 == jank::track::empty) t2.clear();
			if(t3 == jank::track::empty) t3.clear();

			if(not t1.empty() and t1.back() != '?') t1.push_back('?');
			if(not t2.empty() and t2.back() != '?') t2.push_back('?');
			if(not t3.empty() and t3.back() != '?') t3.push_back('?');

			std::stringstream ss;

			ss << "\033w\033s\033\1" << t1 << "\033\2" << t2 << "\033\3" << t3 << "\x3f\x1c";

			std::string cmd = ss.str();

			std::stringstream msg;
			msg << "DATA : " << hex(cmd);
			message(msg.str().c_str());

			if(writen(cmd.c_str(), cmd.length()) != (ssize_t)cmd.length())
					return false;

			while(sync() and not cancel()) {

					auto iter = msr_buffer.begin();

					compare_position(iter, msr_buffer, isescape);
					compare_position(iter, msr_buffer, isstatus);

					char status = *std::prev(iter);

					msr_buffer.erase(msr_buffer.begin(), iter);

					msr_errno = (int)(status - '0');

					if(status == '0')
						return true;

					break;
			}

			int e = errno;

			msleep(250);

			reset();
			flush();

			errno = e;

			return false;

	}

	bool msr::read(std::string& track1, std::string& track2, std::string& track3) {
		std::cmatch cm;
		std::string data;
		auto retval = read(data);

		track1 = jank::track::empty;
		track2 = jank::track::empty;
		track3 = jank::track::empty;

		if(false)
			std::cout << "RETURN=" << (retval ? "TRUE" : "FALSE") << " DATA=" << hex(data) << std::endl;

		std::regex e("^\\x1b\\x01(.*)\\x1b\\x02(.*)\\x1b\\x03(.*)");

		std::regex_match(data.c_str(), cm, e);

		if(false)
			std::cout << "REGEX_MATCH := ( CM.SIZE() = " << cm.size() << " )" << std::endl;

		if(false)
			for(size_t n = 0; n < cm.size(); n++)
				std::cout << "MATCH " << n << "(" << cm.length(n) << ")" << " := " << hex(cm.str(n)) << std::endl;

		if(cm.size() == 4) {
			track1 = std::string(cm.str(1).c_str(), cm.length(1));
			track2 = std::string(cm.str(2).c_str(), cm.length(2));
			track3 = std::string(cm.str(3).c_str(), cm.length(3));
		}

		return retval;
	}

	bool msr::read(std::string& data) {

		const char cmd[] = { '\033', 'r' };

		message("READ");

		if(writen(cmd, sizeof(cmd)) != sizeof(cmd))
			return false;

		while(sync() and not cancel()) {

			auto iter = msr_buffer.begin();

			compare_position(iter, msr_buffer, isescape);
			compare_position(iter, msr_buffer, is('s'));

			auto data_end = iter;

			for(; data_end != msr_buffer.end(); data_end++) {
				auto jter = data_end;
				compare_position_nf(jter, msr_buffer, is('?'));
				compare_position_nf(jter, msr_buffer, is('\034'));
				compare_position_nf(jter, msr_buffer, isescape);
				compare_position_nf(jter, msr_buffer, isstatus);
				break;
			}

			if(data_end == msr_buffer.end())
				continue;

			while(iter != data_end)
				data.push_back(*iter++);

			compare_position(iter, msr_buffer, is('?'));
			compare_position(iter, msr_buffer, is('\34'));
			compare_position(iter, msr_buffer, isescape);
			compare_position(iter, msr_buffer, isstatus);

			char status = *std::prev(iter);

			msr_buffer.erase(msr_buffer.begin(), iter);

			msr_errno = (int)(status - '0');

			if(status == '0')
				return true;

			break;
		}

		int e = errno;

		msleep(250);

		reset();
		flush();

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

			const char cmd[] = { '\033', 't' };

			message("MODEL");

			if(cache.model != nullptr)
					return *cache.model;

			if(writen(cmd, sizeof(cmd)) != sizeof(cmd))
					return '\0';

			while(sync() and not cancel()) {

					auto iter = msr_buffer.begin();

					char ch;

					if(iter == msr_buffer.end()) continue; if(*iter != '\033') { errno = EPROTO; break; } else iter++;
					if(iter == msr_buffer.end()) continue; if(not isdigit(*iter)) { errno = EPROTO; break; } else ch = *iter++;
					if(iter == msr_buffer.end()) continue; if(*iter != 'S') { errno = EPROTO; break; } else iter++;

					msr_buffer.erase(msr_buffer.begin(), iter);

					cache.model = new char;

					*cache.model = ch;

					return *cache.model;
			}

			int e = errno;

			msleep(250);

			reset();
			flush();

			errno = e;

			return '\0';
	}

	const char *msr::firmware() {

			const char cmd[] = { '\033', 'v' };

			message("FIRMWARE");

			if(cache.firmware != nullptr)
					return cache.firmware;

			if(writen(cmd, sizeof(cmd)) != sizeof(cmd))
					return nullptr;

			while(sync() and not cancel()) {

					auto iter = msr_buffer.begin();

					std::string s;

					if(iter == msr_buffer.end()) continue; if(*iter != '\033') { errno = EPROTO; break; } else iter++;
					if(iter == msr_buffer.end()) continue; if(*iter != 'R') { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(*iter != 'E') { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(*iter != 'V') { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(not isalpha(*iter)) { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(not isdigit(*iter)) { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(*iter != '.') { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(not isdigit(*iter)) { errno = EPROTO; break; } else s.push_back(*iter++);
					if(iter == msr_buffer.end()) continue; if(not isdigit(*iter)) { errno = EPROTO; break; } else s.push_back(*iter++);

					msr_buffer.erase(msr_buffer.begin(), iter);

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

		std::cout << "EXPECT RESPONSE : " << hex(buf, n) << std::endl;

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

	std::string msr::hex(const std::string& s) const {
		return hex(s.c_str(), s.length());
	}

	std::string msr::hex(const char *s, size_t sz) const {

		const std::string underline = "\033[4m";
		const std::string bold = "\033[1m";
		const std::string reset = "\033[0m";

		std::stringstream ss;

		ss << "char s[" << std::dec << sz << "] = { ";

		for(unsigned int n = 0; n < sz; n++) {
			char c = s[n];
			if(false) {
				// NOP
			} else if(c == '\034') {
				ss << bold << "[FS]" << reset;
			} else if(c >= 0 and c <= 27) {
				ss << bold << '^' << (char)(c + '@') << reset;
			} else if(c == '\0') {
				ss << bold << "[NUL]" << reset;
			} else if(c == '\033') {
				ss << bold << "[ESC]" << reset;
			} else if(ispunct(c)) {
				ss << c;
			} else if(isalnum(c)) {
				ss << c;
			} else {
				ss << bold << std::hex << std::setfill('0') << std::setw(2) << (int)c << reset;
			}
		}

		ss << " };";

		return ss.str();
	}
}
