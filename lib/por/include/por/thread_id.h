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
			if(_size == new_size)
				return;

			std::uint16_t* p = new_size > asize ? new std::uint16_t[new_size] : _data.a;

			if(p != data()) {
				for(std::size_t i = 0; i < new_size && i < _size; ++i) {
					p[i] = data()[i];
				}
			}

			if(_size > asize) delete[] _data.p;

			_size = new_size;

			if(new_size > asize) _data.p = p;
		}

		std::uint16_t* data() noexcept { return _size <= asize ? _data.a : _data.p; }

	public:
		thread_id() noexcept : _size(0) {
			_data.p = nullptr;
		}

		thread_id(const thread_id& parent, std::uint16_t localId) : _size(parent._size + 1) {
			assert(localId != 0 && "Local ids must be non-zero");
			_data.p = nullptr;

			std::uint16_t* p = _size > asize ? (_data.p = new std::uint16_t[_size]) : _data.a;
			std::memcpy(p, parent.ids(), (_size - 1) * sizeof(*p));

			data()[_size - 1] = localId;
		}

		explicit thread_id(std::uint16_t localId)
			: _size(1) {
			assert(localId != 0 && "Local ids must be non-zero");
			_data.p = nullptr;

			data()[0] = localId;
		}

		thread_id(thread_id const& other) : _size(other._size) {
			if(_size > asize) {
				_data.p = new std::uint16_t[_size];
			} else {
				_data.p = nullptr;
			}

			const void* from = _size <= asize ? other._data.a : other._data.p;
			std::memcpy(data(), from, _size * sizeof(*data()));
		}

		thread_id(thread_id&& other) noexcept : _size(other._size), _data(other._data) {
			other._size = 0;
		}

		thread_id& operator=(thread_id const& other) noexcept {
			if(&other == this)
				return *this;

			resize(other._size);

			const void* from = _size <= asize ? other._data.a : other._data.p;
			std::memcpy(data(), from, _size * sizeof(*data()));

			return *this;
		}

		thread_id& operator=(thread_id&& other) noexcept {
			if(this != &other) {
				if(_size > asize) delete[] _data.p;
				std::memcpy(this, &other, sizeof(thread_id));
				other._size = 0;
			}
			return *this;
		}

		~thread_id() noexcept { if(_size > asize) delete[] _data.p; }

		auto const& size() const noexcept { return _size; }

		std::uint16_t const* ids() const noexcept { return _size <= asize ? _data.a : _data.p; }

		std::uint16_t localId() const noexcept {
			return _size == 0 ? 0 : ids()[_size - 1];
		}

		std::uint16_t operator[](std::size_t const index) noexcept {
			return ids()[index];
		}

		std::uint16_t const& operator[](std::size_t const index) const noexcept {
			return ids()[index];
		}

		bool empty() noexcept { return _size == 0; };

		friend bool operator==(thread_id const& lhs, thread_id const& rhs) noexcept {
			return lhs.size() == rhs.size() && std::memcmp(lhs.ids(), rhs.ids(), lhs.size() * sizeof(*lhs.ids())) == 0;
		}

		friend bool operator<(thread_id const& lhs, thread_id const& rhs) noexcept {
			return lhs.size() < rhs.size() || std::memcmp(lhs.ids(), rhs.ids(), lhs.size() * sizeof(*lhs.ids())) < 0;
		}

		friend bool operator!=(thread_id const& lhs, thread_id const& rhs) noexcept {
			return !(lhs == rhs);
		}

		explicit operator bool() const noexcept {
			return _size > 0;
		}

		std::string to_string() const {
			std::string res;
			for(std::size_t i = 0; i < size(); i++) {
				if(i > 0)
					res += ',';
				res += std::to_string(ids()[i]);
			}
			return res;
		}

		static std::optional<thread_id> from_string(std::string tidAsString) {
			std::istringstream sstream(tidAsString);
			thread_id tid{};

			while (!sstream.eof()) {
				std::uint16_t lid = 0;

				if (!(sstream >> lid)) {
					return {};
				}

				if (!sstream.eof() && sstream.get() != ',') {
					return {};
				}

				tid = thread_id(tid, lid);
			}

			return tid;
		}
	};

	inline std::ostream &operator<<(std::ostream &os, thread_id const& tid) {
		os << "tid<" << tid.to_string() << ">";
		return os;
	}
}
