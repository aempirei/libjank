#pragma once

#include <string>
#include <list>
#include <array>

#include <unistd.h>

#define msleep(X) usleep((X) * 1000)

namespace jank {

	class msr;


	class msr {

		public:

			using buffer_type = std::list<char>;

			template <size_t N> using pattern_type = std::array<int,N>;

			bool active;

			std::string device;

			long sync_timeout;

			bool start(const char *, int, int);
			bool stop();

			bool sync();
			bool update(int, buffer_type&);
			bool flush();

			bool reset() const;

			bool red() const;
			bool yellow() const;
			bool green() const;
			bool on() const;
			bool off() const;
			bool erase(bool,bool,bool);
			bool erase();
			bool read();

			bool cancel();

			bool test_comm() const;
			bool test_ram() const;
			bool test_sensor() const;

			bool has_track1();
			bool has_track2();
			bool has_track3();

			char model();
			const char *firmware();

			msr();
			~msr();

		private:

			struct {
				char *model;
				char *firmware;
			} cache;

			int msr_fd;
			int oob_fd;
			int msg_fd;

			buffer_type msr_buffer;
			buffer_type oob_buffer;

			constexpr static size_t read_block_sz = 1024;

			const static pattern_type<2> response_ok;
			const static pattern_type<2> response_fail;
			const static pattern_type<2> response_ack;

			bool expect(const void *, size_t, const void *, size_t) const;

			ssize_t writen(const void *, size_t) const;
			ssize_t readn(void *, size_t) const;
			int memncmp(const void *, size_t, const void *, size_t) const;

			std::string hex(const char *, size_t) const;

			template <class T, class U> bool begins_with(const T&, const U&) const;
	};
}
