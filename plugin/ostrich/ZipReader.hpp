// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ostrich 220 -- ROM images out of zip files, as MAME keeps them (u220.zip, u110.zip).
// Only what that needs: the central directory, stored and deflated entries, CRC-checked.
// No zip64, no encryption, no other methods: such entries are skipped.  The directory alone
// says each entry's size and CRC32, so a folder of many zips costs a few reads per zip;
// only a likely match is unpacked.

#ifndef OSTRICH_ZIP_READER_HPP
#define OSTRICH_ZIP_READER_HPP

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace ostrich::zip {

namespace fs = std::filesystem;

struct Entry
{
	std::string name;           ///< as stored, '/'-separated
	uint32_t crc = 0, packed = 0, size = 0, local = 0;
	uint16_t method = 0;        ///< 0 stored, 8 deflated
};

inline uint32_t crc32(const uint8_t *p, size_t n)
{
	static const std::array<uint32_t, 256> table = [] {
		std::array<uint32_t, 256> t {};
		for (uint32_t i = 0; i < 256; i ++)
		{
			uint32_t c = i;
			for (int k = 0; k < 8; k ++)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			t[i] = c;
		}
		return t;
	}();
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i ++)
		c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

// ---- raw deflate (RFC 1951), after Mark Adler's puff: small and strict, not fast --------

class Inflater
{
public:
	Inflater(const uint8_t *in, size_t n) : m_in(in), m_n(n) {}

	/// Decodes the whole stream into @p out, which must come to @p expect bytes.
	bool run(std::vector<uint8_t> &out, size_t expect)
	{
		out.clear();
		out.reserve(expect);
		bool last = false;
		while (!last && !m_err)
		{
			last = bits(1) != 0;
			const unsigned type = bits(2);
			bool ok = false;
			if (type == 0)
				ok = stored(out, expect);
			else if (type == 1)
				ok = fixed() && codes(out, expect);
			else if (type == 2)
				ok = dynamic() && codes(out, expect);
			if (!ok || m_err)
				return false;
		}
		return out.size() == expect;
	}

private:
	struct Huffman
	{
		uint16_t count[16];     ///< codes of each length
		uint16_t symbol[288];   ///< symbols in canonical order
	};

	unsigned bits(int need)
	{
		uint32_t v = m_bitBuf;
		while (m_bitCnt < need)
		{
			if (m_pos >= m_n)
			{
				m_err = true;
				return 0;
			}
			v |= uint32_t(m_in[m_pos ++]) << m_bitCnt;
			m_bitCnt += 8;
		}
		m_bitBuf = v >> need;
		m_bitCnt -= need;
		return v & ((1u << need) - 1);
	}

	bool stored(std::vector<uint8_t> &out, size_t expect)
	{
		m_bitBuf = 0;                       // to the byte boundary
		m_bitCnt = 0;
		if (m_pos + 4 > m_n)
			return false;
		const unsigned len = m_in[m_pos] | m_in[m_pos + 1] << 8;
		const unsigned nlen = m_in[m_pos + 2] | m_in[m_pos + 3] << 8;
		m_pos += 4;
		if (len != (~nlen & 0xFFFF) || m_pos + len > m_n || out.size() + len > expect)
			return false;
		out.insert(out.end(), m_in + m_pos, m_in + m_pos + len);
		m_pos += len;
		return true;
	}

	static bool build(Huffman &h, const uint8_t *lengths, int n)
	{
		for (auto &c : h.count)
			c = 0;
		for (int s = 0; s < n; s ++)
			h.count[lengths[s]] ++;
		if (h.count[0] == n)
			return true;                    // no codes: fine as long as none is read
		int left = 1;
		for (int len = 1; len < 16; len ++)
		{
			left <<= 1;
			left -= h.count[len];
			if (left < 0)
				return false;               // over-subscribed
		}
		uint16_t offs[16];
		offs[1] = 0;
		for (int len = 1; len < 15; len ++)
			offs[len + 1] = uint16_t(offs[len] + h.count[len]);
		for (int s = 0; s < n; s ++)
			if (lengths[s] != 0)
				h.symbol[offs[lengths[s]] ++] = uint16_t(s);
		return true;
	}

	int decode(const Huffman &h)
	{
		int code = 0, first = 0, index = 0;
		for (int len = 1; len < 16; len ++)
		{
			code |= int(bits(1));
			const int count = h.count[len];
			if (code - count < first)
				return h.symbol[index + (code - first)];
			index += count;
			first += count;
			first <<= 1;
			code <<= 1;
		}
		m_err = true;
		return -1;
	}

	bool fixed()
	{
		uint8_t lengths[288 + 30];
		for (int s = 0; s < 144; s ++) lengths[s] = 8;
		for (int s = 144; s < 256; s ++) lengths[s] = 9;
		for (int s = 256; s < 280; s ++) lengths[s] = 7;
		for (int s = 280; s < 288; s ++) lengths[s] = 8;
		for (int s = 288; s < 288 + 30; s ++) lengths[s] = 5;
		return build(m_lit, lengths, 288) && build(m_dist, lengths + 288, 30);
	}

	bool dynamic()
	{
		static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
		const int nlen = int(bits(5)) + 257, ndist = int(bits(5)) + 1, ncode = int(bits(4)) + 4;
		if (m_err || nlen > 286 || ndist > 30)
			return false;
		uint8_t lengths[286 + 30] = {};
		for (int i = 0; i < ncode; i ++)
			lengths[order[i]] = uint8_t(bits(3));
		Huffman lencode;
		if (!build(lencode, lengths, 19))
			return false;
		for (auto &l : lengths)
			l = 0;
		int index = 0;
		while (index < nlen + ndist)
		{
			int sym = decode(lencode);
			if (sym < 0 || m_err)
				return false;
			if (sym < 16)
			{
				lengths[index ++] = uint8_t(sym);
				continue;
			}
			uint8_t len = 0;
			if (sym == 16)
			{
				if (index == 0)
					return false;
				len = lengths[index - 1];
				sym = 3 + int(bits(2));
			}
			else if (sym == 17)
				sym = 3 + int(bits(3));
			else
				sym = 11 + int(bits(7));
			if (index + sym > nlen + ndist)
				return false;
			while (sym --)
				lengths[index ++] = len;
		}
		if (lengths[256] == 0)
			return false;                   // no end-of-block code
		return build(m_lit, lengths, nlen) && build(m_dist, lengths + nlen, ndist);
	}

	bool codes(std::vector<uint8_t> &out, size_t expect)
	{
		static const uint16_t lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
			35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
		static const uint8_t lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
			3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
		static const uint16_t dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
			257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
		static const uint8_t dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
			7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
		for (;;)
		{
			int sym = decode(m_lit);
			if (sym < 0 || m_err)
				return false;
			if (sym < 256)
			{
				if (out.size() >= expect)
					return false;
				out.push_back(uint8_t(sym));
				continue;
			}
			if (sym == 256)
				return true;
			sym -= 257;
			if (sym >= 29)
				return false;
			const size_t len = lbase[sym] + bits(lext[sym]);
			const int dsym = decode(m_dist);
			if (dsym < 0 || dsym >= 30 || m_err)
				return false;
			const size_t dist = dbase[dsym] + bits(dext[dsym]);
			if (m_err || dist > out.size() || out.size() + len > expect)
				return false;
			for (size_t i = 0; i < len; i ++)
				out.push_back(out[out.size() - dist]);
		}
	}

	const uint8_t *m_in;
	size_t m_n, m_pos = 0;
	uint32_t m_bitBuf = 0;
	int m_bitCnt = 0;
	bool m_err = false;
	Huffman m_lit {}, m_dist {};
};

// ---- the zip file ----------------------------------------------------------------------

namespace detail {

inline FILE *openRead(const fs::path &p)
{
#ifdef _WIN32
	return _wfopen(p.c_str(), L"rb");
#else
	return std::fopen(p.c_str(), "rb");
#endif
}

inline bool readAt(FILE *f, uint32_t off, void *dst, size_t n)
{
	return std::fseek(f, long(off), SEEK_SET) == 0 && std::fread(dst, 1, n, f) == n;
}

inline uint32_t le16(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
inline uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

struct Closer { FILE *f; ~Closer() { if (f) std::fclose(f); } };

} // namespace detail

/// The entries of a zip, from its central directory; empty if it is none (or too odd).
inline std::vector<Entry> list(const fs::path &path)
{
	using namespace detail;
	std::vector<Entry> out;
	std::error_code ec;
	const auto size = fs::file_size(path, ec);
	if (ec || size < 22 || size > 0x7FFFFFFF)
		return out;
	Closer c { openRead(path) };
	if (!c.f)
		return out;

	// The end record, within the last 64 KB + 22 (its comment).
	const uint32_t tail = uint32_t(size < 0x10000 + 22 ? size : 0x10000 + 22);
	std::vector<uint8_t> t(tail);
	if (!readAt(c.f, uint32_t(size - tail), t.data(), tail))
		return out;
	size_t eocd = std::string::npos;
	for (size_t i = tail - 22 + 1; i -- > 0; )
		if (t[i] == 'P' && t[i + 1] == 'K' && t[i + 2] == 5 && t[i + 3] == 6)
		{
			eocd = i;
			break;
		}
	if (eocd == std::string::npos)
		return out;
	const uint32_t cdSize = le32(&t[eocd + 12]), cdOff = le32(&t[eocd + 16]);
	if (uint64_t(cdOff) + cdSize > size || cdSize > 0x1000000)
		return out;
	std::vector<uint8_t> cd(cdSize);
	if (!readAt(c.f, cdOff, cd.data(), cdSize))
		return out;

	for (size_t p = 0; p + 46 <= cd.size(); )
	{
		const uint8_t *r = &cd[p];
		if (le32(r) != 0x02014b50)
			break;
		const uint32_t nameLen = le16(r + 28), extraLen = le16(r + 30), commentLen = le16(r + 32);
		if (p + 46 + nameLen > cd.size())
			break;
		Entry e;
		e.method = uint16_t(le16(r + 10));
		e.crc = le32(r + 16);
		e.packed = le32(r + 20);
		e.size = le32(r + 24);
		e.local = le32(r + 42);
		e.name.assign(reinterpret_cast<const char *>(r + 46), nameLen);
		if ((le16(r + 8) & 1) == 0)         // not encrypted
			out.push_back(std::move(e));
		p += 46 + nameLen + extraLen + commentLen;
	}
	return out;
}

/// Unpacks one entry, its size and CRC checked.  False for anything it cannot read.
inline bool extract(const fs::path &path, const Entry &e, std::vector<uint8_t> &out)
{
	using namespace detail;
	out.clear();
	if ((e.method != 0 && e.method != 8) || e.size > 0x4000000 || e.packed > 0x4000000)
		return false;
	Closer c { openRead(path) };
	uint8_t h[30];
	if (!c.f || !readAt(c.f, e.local, h, sizeof(h)) || le32(h) != 0x04034b50)
		return false;
	const uint32_t data = e.local + 30 + le16(h + 26) + le16(h + 28);
	std::vector<uint8_t> packed(e.packed);
	if (!readAt(c.f, data, packed.data(), packed.size()))
		return false;
	if (e.method == 0)
	{
		if (e.packed != e.size)
			return false;
		out.swap(packed);
	}
	else if (!Inflater(packed.data(), packed.size()).run(out, e.size))
	{
		out.clear();
		return false;
	}
	if (crc32(out.data(), out.size()) != e.crc)
	{
		out.clear();
		return false;
	}
	return true;
}

} // namespace ostrich::zip

#endif // OSTRICH_ZIP_READER_HPP
