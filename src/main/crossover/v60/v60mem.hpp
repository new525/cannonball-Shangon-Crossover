/***************************************************************************
    Memory access for the V60/V70 port: MAME 0.264's address-space access rules.

    memory_read_generic / memory_write_generic below are MAME 0.264 src/emu/emumem.h,
    verbatim (license:BSD-3-Clause, copyright-holders:Aaron Giles, Olivier Galibert); only the
    emu::detail:: qualifier is dropped. They split a byte/word/dword access, aligned or not,
    into native-width accesses with a mem_mask, exactly as MAME's address_space and
    memory_access cache do (address_space_specific::read_byte etc. in emumem_aspace.cpp,
    memory_access_cache::read_byte etc. in emumem.h).

    Space32 models MAME's 32-bit little-endian program space of the V70 (and, with
    Fetch = true, its memory_access<32, 2, 0, ENDIANNESS_LITTLE>::cache m_cache32);
    SpaceIo16 the 16-bit, 24-bit-address io space (m_io_config).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

struct V60Bus;

namespace v60mem
{
    typedef uint8_t  u8;
    typedef uint16_t u16;
    typedef uint32_t u32;
    typedef uint64_t u64;
    typedef uint32_t offs_t;

    template<int W> struct size_of_width;
    template<> struct size_of_width<0> { using type = u8; };
    template<> struct size_of_width<1> { using type = u16; };
    template<> struct size_of_width<2> { using type = u32; };
    template<> struct size_of_width<3> { using type = u64; };
    template<int W> using handler_entry_size_t = typename size_of_width<W>::type;   // emu::detail::handler_entry_size_t

    enum endianness_t { ENDIANNESS_LITTLE, ENDIANNESS_BIG };

    constexpr int iabs(int v) { return v < 0 ? -v : v; }
    template <typename T> constexpr T make_bitmask(unsigned n) { return T((n < (8 * sizeof(T)) ? (std::make_unsigned_t<T>(1) << n) : std::make_unsigned_t<T>(0)) - 1); }

// ======================> address offset -> byte offset

constexpr offs_t memory_offset_to_byte(offs_t offset, int AddrShift) { return AddrShift < 0 ? offset << iabs(AddrShift) : offset >> iabs(AddrShift); }

// ======================> generic read/write decomposition routines


// generic direct read
template<int Width, int AddrShift, endianness_t Endian, int TargetWidth, bool Aligned, typename T> handler_entry_size_t<TargetWidth>  memory_read_generic(T rop, offs_t address, handler_entry_size_t<TargetWidth> mask)
{
	using TargetType = handler_entry_size_t<TargetWidth>;
	using NativeType = handler_entry_size_t<Width>;

	constexpr u32 TARGET_BYTES = 1 << TargetWidth;
	constexpr u32 TARGET_BITS = 8 * TARGET_BYTES;
	constexpr u32 NATIVE_BYTES = 1 << Width;
	constexpr u32 NATIVE_BITS = 8 * NATIVE_BYTES;
	constexpr u32 NATIVE_STEP = AddrShift >= 0 ? NATIVE_BYTES << iabs(AddrShift) : NATIVE_BYTES >> iabs(AddrShift);
	constexpr u32 NATIVE_MASK = Width + AddrShift >= 0 ? make_bitmask<u32>(Width + AddrShift) : 0;

	// equal to native size and aligned; simple pass-through to the native reader
	if (NATIVE_BYTES == TARGET_BYTES && (Aligned || (address & NATIVE_MASK) == 0))
		return rop(address & ~NATIVE_MASK, mask);

	// if native size is larger, see if we can do a single masked read (guaranteed if we're aligned)
	if (NATIVE_BYTES > TARGET_BYTES)
	{
		u32 offsbits = 8 * (memory_offset_to_byte(address, AddrShift) & (NATIVE_BYTES - (Aligned ? TARGET_BYTES : 1)));
		if (Aligned || (offsbits + TARGET_BITS <= NATIVE_BITS))
		{
			if (Endian != ENDIANNESS_LITTLE) offsbits = NATIVE_BITS - TARGET_BITS - offsbits;
			return rop(address & ~NATIVE_MASK, (NativeType)mask << offsbits) >> offsbits;
		}
	}

	// determine our alignment against the native boundaries, and mask the address
	u32 offsbits = 8 * (memory_offset_to_byte(address, AddrShift) & (NATIVE_BYTES - 1));
	address &= ~NATIVE_MASK;

	// if we're here, and native size is larger or equal to the target, we need exactly 2 reads
	if (NATIVE_BYTES >= TARGET_BYTES)
	{
		// little-endian case
		if (Endian == ENDIANNESS_LITTLE)
		{
			// read lower bits from lower address
			TargetType result = 0;
			NativeType curmask = (NativeType)mask << offsbits;
			if (curmask != 0) result = rop(address, curmask) >> offsbits;

			// read upper bits from upper address
			offsbits = NATIVE_BITS - offsbits;
			curmask = mask >> offsbits;
			if (curmask != 0) result |= rop(address + NATIVE_STEP, curmask) << offsbits;
			return result;
		}

		// big-endian case
		else
		{
			// left-justify the mask to the target type
			constexpr u32 LEFT_JUSTIFY_TARGET_TO_NATIVE_SHIFT = ((NATIVE_BITS >= TARGET_BITS) ? (NATIVE_BITS - TARGET_BITS) : 0);
			NativeType result = 0;
			NativeType ljmask = (NativeType)mask << LEFT_JUSTIFY_TARGET_TO_NATIVE_SHIFT;
			NativeType curmask = ljmask >> offsbits;

			// read upper bits from lower address
			if (curmask != 0) result = rop(address, curmask) << offsbits;
			offsbits = NATIVE_BITS - offsbits;

			// read lower bits from upper address
			curmask = ljmask << offsbits;
			if (curmask != 0) result |= rop(address + NATIVE_STEP, curmask) >> offsbits;

			// return the un-justified result
			return result >> LEFT_JUSTIFY_TARGET_TO_NATIVE_SHIFT;
		}
	}

	// if we're here, then we have 2 or more reads needed to get our final result
	else
	{
		// compute the maximum number of loops; we do it this way so that there are
		// a fixed number of loops for the compiler to unroll if it desires
		constexpr u32 MAX_SPLITS_MINUS_ONE = TARGET_BYTES / NATIVE_BYTES - 1;
		TargetType result = 0;

		// little-endian case
		if (Endian == ENDIANNESS_LITTLE)
		{
				// read lowest bits from first address
			NativeType curmask = mask << offsbits;
			if (curmask != 0) result = rop(address, curmask) >> offsbits;

			// read middle bits from subsequent addresses
			offsbits = NATIVE_BITS - offsbits;
			for (u32 index = 0; index < MAX_SPLITS_MINUS_ONE; index++)
			{
				address += NATIVE_STEP;
				curmask = mask >> offsbits;
				if (curmask != 0) result |= (TargetType)rop(address, curmask) << offsbits;
				offsbits += NATIVE_BITS;
			}

			// if we're not aligned and we still have bits left, read uppermost bits from last address
			if (!Aligned && offsbits < TARGET_BITS)
			{
				curmask = mask >> offsbits;
				if (curmask != 0) result |= (TargetType)rop(address + NATIVE_STEP, curmask) << offsbits;
			}
		}

		// big-endian case
		else
		{
			// read highest bits from first address
			offsbits = TARGET_BITS - (NATIVE_BITS - offsbits);
			NativeType curmask = mask >> offsbits;
			if (curmask != 0) result = (TargetType)rop(address, curmask) << offsbits;

			// read middle bits from subsequent addresses
			for (u32 index = 0; index < MAX_SPLITS_MINUS_ONE; index++)
			{
				offsbits -= NATIVE_BITS;
				address += NATIVE_STEP;
				curmask = mask >> offsbits;
				if (curmask != 0) result |= (TargetType)rop(address, curmask) << offsbits;
			}

			// if we're not aligned and we still have bits left, read lowermost bits from the last address
			if (!Aligned && offsbits != 0)
			{
				offsbits = NATIVE_BITS - offsbits;
				curmask = mask << offsbits;
				if (curmask != 0) result |= rop(address + NATIVE_STEP, curmask) >> offsbits;
			}
		}
		return result;
	}
}

// generic direct write
template<int Width, int AddrShift, endianness_t Endian, int TargetWidth, bool Aligned, typename T> void memory_write_generic(T wop, offs_t address, handler_entry_size_t<TargetWidth> data, handler_entry_size_t<TargetWidth> mask)
{
	using NativeType = handler_entry_size_t<Width>;

	constexpr u32 TARGET_BYTES = 1 << TargetWidth;
	constexpr u32 TARGET_BITS = 8 * TARGET_BYTES;
	constexpr u32 NATIVE_BYTES = 1 << Width;
	constexpr u32 NATIVE_BITS = 8 * NATIVE_BYTES;
	constexpr u32 NATIVE_STEP = AddrShift >= 0 ? NATIVE_BYTES << iabs(AddrShift) : NATIVE_BYTES >> iabs(AddrShift);
	constexpr u32 NATIVE_MASK = Width + AddrShift >= 0 ? (1 << (Width + AddrShift)) - 1 : 0;

	// equal to native size and aligned; simple pass-through to the native writer
	if (NATIVE_BYTES == TARGET_BYTES && (Aligned || (address & NATIVE_MASK) == 0))
		return wop(address & ~NATIVE_MASK, data, mask);

	// if native size is larger, see if we can do a single masked write (guaranteed if we're aligned)
	if (NATIVE_BYTES > TARGET_BYTES)
	{
		u32 offsbits = 8 * (memory_offset_to_byte(address, AddrShift) & (NATIVE_BYTES - (Aligned ? TARGET_BYTES : 1)));
		if (Aligned || (offsbits + TARGET_BITS <= NATIVE_BITS))
		{
			if (Endian != ENDIANNESS_LITTLE) offsbits = NATIVE_BITS - TARGET_BITS - offsbits;
			return wop(address & ~NATIVE_MASK, (NativeType)data << offsbits, (NativeType)mask << offsbits);
		}
	}

	// determine our alignment against the native boundaries, and mask the address
	u32 offsbits = 8 * (memory_offset_to_byte(address, AddrShift) & (NATIVE_BYTES - 1));
	address &= ~NATIVE_MASK;

	// if we're here, and native size is larger or equal to the target, we need exactly 2 writes
	if (NATIVE_BYTES >= TARGET_BYTES)
	{
		// little-endian case
		if (Endian == ENDIANNESS_LITTLE)
		{
			// write lower bits to lower address
			NativeType curmask = (NativeType)mask << offsbits;
			if (curmask != 0) wop(address, (NativeType)data << offsbits, curmask);

			// write upper bits to upper address
			offsbits = NATIVE_BITS - offsbits;
			curmask = mask >> offsbits;
			if (curmask != 0) wop(address + NATIVE_STEP, data >> offsbits, curmask);
		}

		// big-endian case
		else
		{
			// left-justify the mask and data to the target type
			constexpr u32 LEFT_JUSTIFY_TARGET_TO_NATIVE_SHIFT = ((NATIVE_BITS >= TARGET_BITS) ? (NATIVE_BITS - TARGET_BITS) : 0);
			NativeType ljdata = (NativeType)data << LEFT_JUSTIFY_TARGET_TO_NATIVE_SHIFT;
			NativeType ljmask = (NativeType)mask << LEFT_JUSTIFY_TARGET_TO_NATIVE_SHIFT;
				// write upper bits to lower address
			NativeType curmask = ljmask >> offsbits;
			if (curmask != 0) wop(address, ljdata >> offsbits, curmask);
				// write lower bits to upper address
			offsbits = NATIVE_BITS - offsbits;
			curmask = ljmask << offsbits;
			if (curmask != 0) wop(address + NATIVE_STEP, ljdata << offsbits, curmask);
		}
	}

	// if we're here, then we have 2 or more writes needed to get our final result
	else
	{
		// compute the maximum number of loops; we do it this way so that there are
		// a fixed number of loops for the compiler to unroll if it desires
		constexpr u32 MAX_SPLITS_MINUS_ONE = TARGET_BYTES / NATIVE_BYTES - 1;

		// little-endian case
		if (Endian == ENDIANNESS_LITTLE)
		{
			// write lowest bits to first address
			NativeType curmask = mask << offsbits;
			if (curmask != 0) wop(address, data << offsbits, curmask);

			// write middle bits to subsequent addresses
			offsbits = NATIVE_BITS - offsbits;
			for (u32 index = 0; index < MAX_SPLITS_MINUS_ONE; index++)
			{
				address += NATIVE_STEP;
				curmask = mask >> offsbits;
				if (curmask != 0) wop(address, data >> offsbits, curmask);
				offsbits += NATIVE_BITS;
			}

			// if we're not aligned and we still have bits left, write uppermost bits to last address
			if (!Aligned && offsbits < TARGET_BITS)
			{
				curmask = mask >> offsbits;
				if (curmask != 0) wop(address + NATIVE_STEP, data >> offsbits, curmask);
			}
		}

		// big-endian case
		else
		{
			// write highest bits to first address
			offsbits = TARGET_BITS - (NATIVE_BITS - offsbits);
			NativeType curmask = mask >> offsbits;
			if (curmask != 0) wop(address, data >> offsbits, curmask);

			// write middle bits to subsequent addresses
			for (u32 index = 0; index < MAX_SPLITS_MINUS_ONE; index++)
			{
				offsbits -= NATIVE_BITS;
				address += NATIVE_STEP;
				curmask = mask >> offsbits;
				if (curmask != 0) wop(address, data >> offsbits, curmask);
			}

			// if we're not aligned and we still have bits left, write lowermost bits to the last address
			if (!Aligned && offsbits != 0)
			{
				offsbits = NATIVE_BITS - offsbits;
				curmask = mask << offsbits;
				if (curmask != 0) wop(address + NATIVE_STEP, data << offsbits, curmask);
			}
		}
	}
}

// ======================> the V70's spaces

    // A 64 KB page of directly addressed ROM/RAM (PORT: stands in for MAME's
    // handler_entry_read_memory / handler_entry_write_memory on those ranges).
    struct DirectPage
    {
        uint8_t* r = nullptr;   // page base for reads, or nullptr: use the bus
        uint8_t* w = nullptr;   // page base for writes, or nullptr: use the bus
    };

    // Program space (Fetch = false: address_space_specific<…, 2, 0, ENDIANNESS_LITTLE>) and
    // opcode cache (Fetch = true: memory_access_cache<2, 0, ENDIANNESS_LITTLE>).
    template <bool Fetch> class Space32
    {
    public:
        using NativeType = u32;
        static constexpr int Width = 2;
        static constexpr int AddrShift = 0;
        static constexpr endianness_t Endian = ENDIANNESS_LITTLE;
        static constexpr u32 NATIVE_MASK = Width + AddrShift >= 0 ? (1 << (Width + AddrShift)) - 1 : 0;

        V60Bus*           m_bus = nullptr;
        const DirectPage* m_pages = nullptr;
        u32               m_pagemask = 0;    // board decode mask (global_mask) for the page lookup

        // read_native / write_native. PORT: MAME applies the space's m_addrmask (the map's
        // global_mask) here before dispatching; the board does that on the bus path, and
        // the direct page lookup uses the same mask.
        inline NativeType read_native(offs_t address, NativeType mask = ~NativeType(0));
        inline void write_native(offs_t address, NativeType data, NativeType mask = ~NativeType(0));

        auto rop() { return [this](offs_t offset, NativeType mask) -> NativeType { return read_native(offset, mask); }; }
        auto wop() { return [this](offs_t offset, NativeType data, NativeType mask) -> void { write_native(offset, data, mask); }; }

        u8  read_byte(offs_t address) { if constexpr(Width == 0) return read_native(address & ~NATIVE_MASK); else return memory_read_generic<Width, AddrShift, Endian, 0, true>(rop(), address, 0xff); }
        u16 read_word(offs_t address) { if constexpr(Width == 1) return read_native(address & ~NATIVE_MASK); else return memory_read_generic<Width, AddrShift, Endian, 1, true>(rop(), address, 0xffff); }
        u16 read_word_unaligned(offs_t address) { return memory_read_generic<Width, AddrShift, Endian, 1, false>(rop(), address, 0xffff); }
        u32 read_dword(offs_t address) { if constexpr(Width == 2) return read_native(address & ~NATIVE_MASK); else return memory_read_generic<Width, AddrShift, Endian, 2, true>(rop(), address, 0xffffffff); }
        u32 read_dword_unaligned(offs_t address) { return memory_read_generic<Width, AddrShift, Endian, 2, false>(rop(), address, 0xffffffff); }

        void write_byte(offs_t address, u8 data) { if constexpr(Width == 0) write_native(address & ~NATIVE_MASK, data); else memory_write_generic<Width, AddrShift, Endian, 0, true>(wop(), address, data, 0xff); }
        void write_word(offs_t address, u16 data) { if constexpr(Width == 1) write_native(address & ~NATIVE_MASK, data); else memory_write_generic<Width, AddrShift, Endian, 1, true>(wop(), address, data, 0xffff); }
        void write_word_unaligned(offs_t address, u16 data) { memory_write_generic<Width, AddrShift, Endian, 1, false>(wop(), address, data, 0xffff); }
        void write_dword(offs_t address, u32 data) { if constexpr(Width == 2) write_native(address & ~NATIVE_MASK, data); else memory_write_generic<Width, AddrShift, Endian, 2, true>(wop(), address, data, 0xffffffff); }
        void write_dword_unaligned(offs_t address, u32 data) { memory_write_generic<Width, AddrShift, Endian, 2, false>(wop(), address, data, 0xffffffff); }
    };

    // io space: address_space_specific<…, 1, 0, ENDIANNESS_LITTLE>, 24 address bits.
    class SpaceIo16
    {
    public:
        using NativeType = u16;
        static constexpr int Width = 1;
        static constexpr int AddrShift = 0;
        static constexpr endianness_t Endian = ENDIANNESS_LITTLE;
        static constexpr u32 NATIVE_MASK = Width + AddrShift >= 0 ? (1 << (Width + AddrShift)) - 1 : 0;
        static constexpr u32 m_addrmask = 0x00ffffff;   // make_bitmask(addr_width 24)

        V60Bus* m_bus = nullptr;

        inline NativeType read_native(offs_t offset, NativeType mask = ~NativeType(0));
        inline void write_native(offs_t offset, NativeType data, NativeType mask = ~NativeType(0));

        auto rop() { return [this](offs_t offset, NativeType mask) -> NativeType { return read_native(offset, mask); }; }
        auto wop() { return [this](offs_t offset, NativeType data, NativeType mask) -> void { write_native(offset, data, mask); }; }

        u8  read_byte(offs_t address) { if constexpr(Width == 0) return read_native(address & ~NATIVE_MASK); else return memory_read_generic<Width, AddrShift, Endian, 0, true>(rop(), address, 0xff); }
        u16 read_word(offs_t address) { if constexpr(Width == 1) return read_native(address & ~NATIVE_MASK); else return memory_read_generic<Width, AddrShift, Endian, 1, true>(rop(), address, 0xffff); }
        u16 read_word_unaligned(offs_t address) { return memory_read_generic<Width, AddrShift, Endian, 1, false>(rop(), address, 0xffff); }
        u32 read_dword(offs_t address) { if constexpr(Width == 2) return read_native(address & ~NATIVE_MASK); else return memory_read_generic<Width, AddrShift, Endian, 2, true>(rop(), address, 0xffffffff); }
        u32 read_dword_unaligned(offs_t address) { return memory_read_generic<Width, AddrShift, Endian, 2, false>(rop(), address, 0xffffffff); }

        void write_byte(offs_t address, u8 data) { if constexpr(Width == 0) write_native(address & ~NATIVE_MASK, data); else memory_write_generic<Width, AddrShift, Endian, 0, true>(wop(), address, data, 0xff); }
        void write_word(offs_t address, u16 data) { if constexpr(Width == 1) write_native(address & ~NATIVE_MASK, data); else memory_write_generic<Width, AddrShift, Endian, 1, true>(wop(), address, data, 0xffff); }
        void write_word_unaligned(offs_t address, u16 data) { memory_write_generic<Width, AddrShift, Endian, 1, false>(wop(), address, data, 0xffff); }
        void write_dword(offs_t address, u32 data) { if constexpr(Width == 2) write_native(address & ~NATIVE_MASK, data); else memory_write_generic<Width, AddrShift, Endian, 2, true>(wop(), address, data, 0xffffffff); }
        void write_dword_unaligned(offs_t address, u32 data) { memory_write_generic<Width, AddrShift, Endian, 2, false>(wop(), address, data, 0xffffffff); }
    };
}
