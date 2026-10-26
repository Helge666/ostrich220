// SPDX-License-Identifier: GPL-3.0-or-later
//
// Watches the host's windows around the panel for the freeze seen in Cubase 15 (Windows,
// 26.10.2026): the host's plugin frame loses WS_VISIBLE while it stays on screen -- what
// WM_SETREDRAW(FALSE) does -- and no WM_PAINT reaches the panel any more, for good.
//
// Logs every WM_SETREDRAW, show and hide the host sends to the panel's ancestors, with the
// call stack as module+offset (is our code on it?), to ostrich-ui.log.  A frame that was
// switched off by WM_SETREDRAW, not hidden since and still off after two seconds is
// switched back on.  A no-op outside Windows.

#ifndef OSTRICH_FRAME_WATCH_HPP
#define OSTRICH_FRAME_WATCH_HPP

#include <cstddef>
#include <cstdint>

namespace ostrich {

class FrameWatch
{
public:
	explicit FrameWatch(uintptr_t view);
	~FrameWatch();

	/// From the UI's idle: heals a frame left switched off.
	void idle();

	/// A few words for the once-a-second log line.
	void status(char *out, size_t size) const;

	struct Impl;

private:
	Impl *m_impl = nullptr;
};

} // namespace ostrich

#endif // OSTRICH_FRAME_WATCH_HPP
