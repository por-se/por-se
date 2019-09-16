#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <optional>
#include <sstream>

namespace por {
	class thread_id {
	private:
		static const std::size_t asize = 4;

		std::size_t _size;

		union data_t {
			std::uint16_t* p;
			std::uint16_t a[asize];
		} _data;

		void resize(std::size_t new_size) {
			if(_size == new_size) {
				return;
			}

			std::uint16_t* p = new_size > asize ? new std::uint16_t[new_size] : _data.a;
			std::uint16_t* q = data();

			if(p != q) {
				for(std::size_t i = 0; i < new_size && i < _size; ++i) {
					p[i] = q[i];
				}
			}

			if(_size > asize) {
				delete[] q;
			}

			_size = new_size;
			if(new_size > asize) {
				_data.p = p;
			}
		}

		void clear() noexcept {
			if(_size > asize) {
				delete[] _data.p;
			}
			_size = 0;
		}

		std::uint16_t* data() noexcept { return _size <= asize ? _data.a : _data.p; }
		std::uint16_t const* data() const noexcept { return _size <= asize ? _data.a : _data.p; }

	public:
		thread_id() noexcept : _size(0) { }

		thread_id(thread_id const& parent, std::uint16_t localId) : thread_id() {
			assert(localId != 0 && "Local ids must be non-zero");

			resize(parent._size + 1);
			std::memcpy(data(), parent.data(), parent._size * sizeof(*data()));
			data()[_size - 1] = localId;
		}

		explicit thread_id(std::uint16_t localId) : thread_id(thread_id(), localId) { }

		thread_id(thread_id const& other) : thread_id() {
			resize(other._size);
			std::memcpy(data(), other.data(), other._size * sizeof(*data()));
		}

		thread_id& operator=(thread_id const& other) noexcept {
			if(this != &other) {
				resize(other._size);
				std::memcpy(data(), other.data(), other._size * sizeof(*data()));
			}
			return *this;
		}

		thread_id(thread_id&& other) noexcept : _size(other._size), _data(other._data) {
			other._size = 0;
		}

		thread_id& operator=(thread_id&& other) noexcept {
			if(this != &other) {
				clear();
				_size = other._size;
				_data = other._data;
				other._size = 0;
			}
			return *this;
		}

		~thread_id() noexcept { clear(); }

		bool empty() const noexcept { return _size == 0; }
		explicit operator bool() const noexcept { return !empty(); }
		std::size_t size() const noexcept { return _size; }

		std::uint16_t const* ids() const noexcept { return data(); }

		std::uint16_t operator[](std::size_t const index) const noexcept {
			return data()[index];
		}

		auto begin() noexcept { return data(); }
		auto begin() const noexcept { return data(); }
		auto end() noexcept { return data() + size(); }
		auto end() const noexcept { return data() + size(); }

		friend bool operator==(thread_id const& lhs, thread_id const& rhs) noexcept {
			return lhs.size() == rhs.size() && std::memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(*lhs.data())) == 0;
		}

		friend bool operator!=(thread_id const& lhs, thread_id const& rhs) noexcept {
			return !(lhs == rhs);
		}

		friend bool operator<(thread_id const& lhs, thread_id const& rhs) noexcept {
			for(std::size_t i = 0; i < lhs.size() && i < rhs.size(); ++i) {
				if(lhs.data()[i] < rhs.data()[i]) {
					return true;
				} else if(rhs.data()[i] < lhs.data()[i]) {
					return false;
				}
			}
			return lhs.size() < rhs.size();
		}

		friend bool operator>(thread_id const& lhs, thread_id const& rhs) noexcept {
			for(std::size_t i = 0; i < lhs.size() && i < rhs.size(); ++i) {
				if(lhs.data()[i] > rhs.data()[i]) {
					return true;
				} else if(rhs.data()[i] > lhs.data()[i]) {
					return false;
				}
			}
			return lhs.size() > rhs.size();
		}

		friend bool operator<=(thread_id const& lhs, thread_id const& rhs) noexcept {
			for(std::size_t i = 0; i < lhs.size() && i < rhs.size(); ++i) {
				if(lhs.data()[i] < rhs.data()[i]) {
					return true;
				} else if(rhs.data()[i] < lhs.data()[i]) {
					return false;
				}
			}
			return lhs.size() <= rhs.size();
		}

		friend bool operator>=(thread_id const& lhs, thread_id const& rhs) noexcept {
			for(std::size_t i = 0; i < lhs.size() && i < rhs.size(); ++i) {
				if(lhs.data()[i] > rhs.data()[i]) {
					return true;
				} else if(rhs.data()[i] > lhs.data()[i]) {
					return false;
				}
			}
			return lhs.size() >= rhs.size();
		}

		friend std::ostream& operator<<(std::ostream& os, thread_id const& tid) {
			bool first = true;
			for(auto entry : tid) {
				if(!first) {
					os << ',';
				}

				os << entry;
				first = false;
			}
			return os;
		}

		std::string to_string() const {
			std::ostringstream buffer;
			buffer << *this;
			return buffer.str();
		}

		static std::optional<thread_id> from_string(std::string const& tid_as_string) {
			std::istringstream sstream(tid_as_string);
			if (!sstream) {
				return {};
			}

			thread_id tid{};

			for (;;) {
				char nextChar = sstream.peek();
				if (nextChar < '0' || nextChar > '9') {
					// We expected the start of a local id but this is not a number
					return {};
				}

				// First read the local identifier
				std::uint16_t lid = 0;
				sstream >> lid;

				// If the stream either fails or it was not possible to read a correct lid, then
				// return an empty optional
				if (sstream.fail() || sstream.bad() || lid == 0) {
					return {};
				}

				tid = thread_id(tid, lid);

				if (sstream.eof()) {
					break;
				}

				// If the stream did not end and after the lid and no colon is present, then
				// the tid has to be in an invalid format -> failure
				if (sstream.get() != ',') {
					return {};
				}
			}

			return tid;
		}
	};
}
