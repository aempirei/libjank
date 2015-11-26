#pragma once

#include <string>

#include <unistd.h>

#define ESC "\033"
#define msleep(X) usleep((X) * 1000)

namespace jank {

	class msr;


	class msr {

			public:

					std::string device;
					bool active;
					int fd;

					bool start(const char *);
					bool start();
					bool stop();

					bool reset() const;

					bool red() const;
					bool yellow() const;
					bool green() const;
					bool on() const;
					bool off() const;

					bool test_comm() const;
					bool test_ram() const;
					bool test_sensor() const;

					bool has_track1() const;
					bool has_track2() const;
					bool has_track3() const;

					char model() const;
					std::string firmware() const;

					msr();
					~msr();

			private:

					bool expect(const void *, size_t, const void *, size_t) const;

					ssize_t writen(const void *, size_t) const;
					ssize_t readn(void *, size_t) const;
					int memncmp(const void *, size_t, const void *, size_t) const;

					std::string hex(const char *, size_t) const;
	};
}
