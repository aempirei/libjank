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

			using parsing_function_type = bool (buffer_type&, std::list<std::string>&);

			template <size_t N> using pattern_type = std::array<int,N>;

			template <size_t N> parsing_function_type *basic_parser(pattern_type<N>&);

			parsing_function_type *multi_expect(const std::list<parsing_function_type *>&, buffer_type&, std::list<std::string>&);

			std::string device;
			bool active;

			int msr_fd;
			int oob_fd;
			int msg_fd;

			buffer_type msr_buffer;
			buffer_type oob_buffer;

			bool start(const char *, int, int);
			bool stop();

			bool sync();
			bool update(int, buffer_type&);

			bool reset() const;

			bool red() const;
			bool yellow() const;
			bool green() const;
			bool on() const;
			bool off() const;
			bool erase(bool,bool,bool) const;
			bool erase() const;

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

			constexpr static long sync_timeout = 5;
			constexpr static size_t read_block_sz = 1024;

			constexpr static pattern_type<2> response_ok = { '\033', '0' };
			constexpr static pattern_type<2> response_fail = { '\033', 'A' };
			constexpr static pattern_type<2> response_ack = { '\033', 'y' };

			bool expect(const void *, size_t, const void *, size_t) const;

			ssize_t writen(const void *, size_t) const;
			ssize_t readn(void *, size_t) const;
			int memncmp(const void *, size_t, const void *, size_t) const;

			std::string hex(const char *, size_t) const;
	};
}
