#include "include/por/configuration.h"

using namespace por;

configuration_iterator::configuration_iterator(por::configuration const& configuration, bool end) {
	_configuration = &configuration;
	if(!end && !configuration.thread_heads().empty()) {
		_thread = configuration.thread_heads().rbegin();
		_event = _thread->second;
	}
}

configuration_iterator& configuration_iterator::operator++() noexcept {
	if(!_event) {
		return *this;
	}

	if(por::event::event const* p = _event->thread_predecessor()) {
		_event = p;
	} else if(_thread != std::prev(_configuration->thread_heads().rend())) {
		++_thread;
		_event = _thread->second;
	} else if(_event == &_configuration->unfolding()->root()) {
		_event = nullptr;
		_thread = decltype(_thread)();
	} else {
		assert(std::next(_thread) == _configuration->thread_heads().rend());
		_event = &_configuration->unfolding()->root();
	}
	return *this;
}
