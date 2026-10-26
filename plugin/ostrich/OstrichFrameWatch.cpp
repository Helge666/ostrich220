// SPDX-License-Identifier: GPL-3.0-or-later
//
// See OstrichFrameWatch.hpp.  Kept apart from OstrichUI.cpp so <windows.h> stays out of
// the DPF/NanoVG namespace.

#include "OstrichFrameWatch.hpp"
#include "OstrichShared.hpp"

#include <cstdio>

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace ostrich {

namespace {

constexpr ULONGLONG kHealAfterMs = 2000;
constexpr unsigned kMaxEventLines = 400;   // per panel; counts go on after that

std::string moduleOffset(void *addr)
{
	HMODULE mod = nullptr;
	char buf[96];
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<LPCSTR>(addr), &mod) || mod == nullptr)
	{
		std::snprintf(buf, sizeof(buf), "?%p", addr);
		return buf;
	}
	char path[MAX_PATH] = "";
	GetModuleFileNameA(mod, path, sizeof(path));
	const char *name = path;
	for (const char *p = path; *p; p ++)
		if (*p == '\\' || *p == '/')
			name = p + 1;
	std::snprintf(buf, sizeof(buf), "%s+%llx", name,
			static_cast<unsigned long long>(static_cast<char *>(addr) - reinterpret_cast<char *>(mod)));
	return buf;
}

std::string stack()
{
	void *frames[32];
	const USHORT n = CaptureStackBackTrace(2, 32, frames, nullptr);
	std::string s;
	for (USHORT i = 0; i < n; i ++)
	{
		if (i)
			s += ' ';
		s += moduleOffset(frames[i]);
	}
	return s;
}

std::string describe(HWND h)
{
	char cls[64] = "";
	GetClassNameA(h, cls, sizeof(cls));
	char buf[128];
	std::snprintf(buf, sizeof(buf), "%p[%s]", static_cast<void *>(h), cls);
	return buf;
}

} // anonymous namespace

struct FrameWatch::Impl
{
	HWND view = nullptr;
	// The ancestor last switched off by WM_SETREDRAW, and since when; cleared when it is
	// switched on again, shown or hidden.
	HWND offWindow = nullptr;
	ULONGLONG offSince = 0;
	unsigned long redrawOff = 0, redrawOn = 0, shows = 0, hides = 0, heals = 0, unexplained = 0;
	unsigned lines = 0;
	bool reportedUnexplained = false;

	void log(const std::string &line)
	{
		if (lines >= kMaxEventLines)
			return;
		lines ++;
		char head[48];
		std::snprintf(head, sizeof(head), "frame %p ", static_cast<void *>(this));
		debugLog("ostrich-ui", (head + line).c_str());
	}

	bool concerns(HWND h) const { return h == view || IsChild(h, view); }

	void message(const CWPSTRUCT &m)
	{
		if (!concerns(m.hwnd))
			return;
		const bool other = (InSendMessageEx(nullptr) & ISMEX_SEND) != 0;
		switch (m.message)
		{
		case WM_SETREDRAW:
			if (m.wParam)
			{
				redrawOn ++;
				if (m.hwnd == offWindow)
					offWindow = nullptr;
				log("SETREDRAW on  " + describe(m.hwnd) + (other ? " (other thread)" : ""));
			}
			else
			{
				redrawOff ++;
				offWindow = m.hwnd;
				offSince = GetTickCount64();
				log("SETREDRAW off " + describe(m.hwnd) + (other ? " (other thread)" : "") + " stack " + stack());
			}
			break;
		case WM_SHOWWINDOW:
			(m.wParam ? shows : hides) ++;
			if (m.hwnd == offWindow)
				offWindow = nullptr;
			log(std::string(m.wParam ? "SHOW " : "HIDE ") + describe(m.hwnd) + " lParam " + std::to_string(m.lParam));
			break;
		case WM_WINDOWPOSCHANGED:
		{
			const WINDOWPOS *wp = reinterpret_cast<const WINDOWPOS *>(m.lParam);
			if (wp->flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW))
			{
				const bool show = (wp->flags & SWP_SHOWWINDOW) != 0;
				(show ? shows : hides) ++;
				if (m.hwnd == offWindow)
					offWindow = nullptr;
				log(std::string(show ? "POS show " : "POS hide ") + describe(m.hwnd));
			}
			else if (!(wp->flags & SWP_NOSIZE))
				log("POS size " + describe(m.hwnd) + " " + std::to_string(wp->cx) + "x" + std::to_string(wp->cy));
			break;
		}
		default:
			break;
		}
	}

	void idle()
	{
		if (view == nullptr || !IsWindow(view))
			return;
		// The first ancestor (the view included) without WS_VISIBLE, if any.
		HWND off = nullptr;
		for (HWND h = view; h != nullptr; h = GetParent(h))
			if (!(GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE))
			{
				off = h;
				break;
			}
		if (off == nullptr)
		{
			reportedUnexplained = false;
			return;
		}
		if (off == offWindow && !IsIconic(GetAncestor(view, GA_ROOT)))
		{
			if (GetTickCount64() - offSince >= kHealAfterMs)
			{
				heals ++;
				log("HEAL " + describe(off) + " left off by WM_SETREDRAW for "
						+ std::to_string(GetTickCount64() - offSince) + " ms");
				offWindow = nullptr;
				SendMessageW(off, WM_SETREDRAW, TRUE, 0);
				RedrawWindow(off, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
			}
		}
		else if (off != offWindow && !reportedUnexplained)
		{
			// Hidden for real, or switched off by a way the hook did not see.
			unexplained ++;
			reportedUnexplained = true;
			log("invisible without SETREDRAW " + describe(off));
		}
	}
};

namespace {

std::vector<FrameWatch::Impl *> g_watches;
HHOOK g_hook = nullptr;

LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam)
{
	if (code == HC_ACTION)
	{
		const CWPSTRUCT &m = *reinterpret_cast<const CWPSTRUCT *>(lParam);
		if (m.message == WM_SETREDRAW || m.message == WM_SHOWWINDOW || m.message == WM_WINDOWPOSCHANGED)
			for (FrameWatch::Impl *w : g_watches)
				w->message(m);
	}
	return CallNextHookEx(g_hook, code, wParam, lParam);
}

} // anonymous namespace

FrameWatch::FrameWatch(uintptr_t view)
	: m_impl(new Impl)
{
	m_impl->view = reinterpret_cast<HWND>(view);
	if (g_hook == nullptr)
		g_hook = SetWindowsHookExW(WH_CALLWNDPROC, hookProc, nullptr, GetCurrentThreadId());
	g_watches.push_back(m_impl);

	const HWND root = GetAncestor(m_impl->view, GA_ROOT);
	m_impl->log("watch " + describe(m_impl->view) + " root " + describe(root)
			+ (g_hook ? " hooked" : " NO HOOK")
			+ (GetWindowThreadProcessId(root, nullptr) == GetCurrentThreadId() ? " same thread" : " OTHER THREAD"));
}

FrameWatch::~FrameWatch()
{
	g_watches.erase(std::remove(g_watches.begin(), g_watches.end(), m_impl), g_watches.end());
	if (g_watches.empty() && g_hook != nullptr)
	{
		UnhookWindowsHookEx(g_hook);
		g_hook = nullptr;
	}
	delete m_impl;
}

void FrameWatch::idle()
{
	m_impl->idle();
}

void FrameWatch::status(char *out, size_t size) const
{
	const Impl &w = *m_impl;
	std::snprintf(out, size, " | frame redraw off %lu on %lu show %lu hide %lu heals %lu unexplained %lu",
			w.redrawOff, w.redrawOn, w.shows, w.hides, w.heals, w.unexplained);
}

} // namespace ostrich

#else

namespace ostrich {

struct FrameWatch::Impl {};

FrameWatch::FrameWatch(uintptr_t) {}
FrameWatch::~FrameWatch() {}
void FrameWatch::idle() {}
void FrameWatch::status(char *out, size_t size) const { if (size) out[0] = '\0'; }

} // namespace ostrich

#endif
