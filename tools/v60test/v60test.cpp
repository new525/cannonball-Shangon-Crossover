// v60test: trace test of the V60/V70 core (src/main/crossover/v60) against MAME 0.264.
//
// Reference data, recorded from MAME 0.264 running orunnersj from power-on:
//   main.tr  debugger trace of the main V70 ("<totalcycles hex> <PC>: <disassembly>",
//            noloop), with MAME's "(interrupted at <PC>, IRQ n)" markers
//   main.io  every main-CPU access in $300000-$EFFFFF, in order:
//            "<seconds> <attoseconds> <addr> <data> <mem_mask>" (reads) / "... W<addr> ..." (writes)
//   main.bin the main program ROM region (2 MB) as MAME loads it
//
// The test board: ROM at $000000-$1FFFFF and $F00000-$FFFFFF, 128 KB work RAM at $200000
// mirrored to $2FFFFF (direct pages), global mask $FFFFFF (segas32.cpp multi32_map); every
// access in $300000-$EFFFFF is replayed from main.io (reads return the logged data, address,
// mask, write data and time are checked). The interrupt controller of segas32.cpp
// (int_control_w / update_irq_state) is modelled from the logged writes; the interrupt
// sources (VBLANK, timers, sound) are not, so at each "(interrupted at ...)" marker the test
// raises the source whose vector leads to the handler seen on the next trace line, unless
// the core already took that interrupt by itself (line still asserted from the model).
// Line changes made by a bus write are queued and applied after the instruction, as MAME's
// input-line event queue does.
//
// usage: v60test main.bin main.tr main.io [options]
//   -o <file>       write the core's trace in MAME's format
//   -romdir <dir>   rebuild the ROM region from the orunnersj ROM files and compare with main.bin
//   -n <count>      stop after <count> trace instructions
//   -bench          also run the span again without tracing (timeslices, hooks off) and time it
#include "crossover/v60/v60.hpp"
#include "crossover/v60/v60d.hpp"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

struct IoEntry
{
    bool     write;
    uint32_t addr, data, mask;
    uint64_t sec, atto;
};

struct TraceEntry
{
    bool        marker;     // "(interrupted at <pc>, IRQ n)"
    uint64_t    cycles;
    uint32_t    pc;
    int         irq;
    std::string text;       // disassembly
    size_t      line;       // 1-based line in main.tr
};

bool read_file(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(size_t(n));
    bool ok = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

// ROM_LOAD32_WORD_x4 / ROM_LOAD32_WORD of orunnersj (segas32.cpp): 16-bit words of each
// file to every other word lane; the x4 files reloaded at +2*length, +4*length, +6*length.
bool build_rom(const std::string& dir, std::vector<uint8_t>& rom)
{
    struct { const char* name; uint32_t offset; uint32_t length; int reloads; } files[] = {
        { "epr-15616.ic37", 0x000000, 0x020000, 4 },
        { "epr-15617.ic40", 0x000002, 0x020000, 4 },
        { "mpr-15538.ic36", 0x100000, 0x080000, 1 },
        { "mpr-15539.ic39", 0x100002, 0x080000, 1 },
    };
    rom.assign(0x200000, 0);
    for (auto& f : files)
    {
        std::vector<uint8_t> data;
        if (!read_file(dir + "/" + f.name, data) || data.size() != f.length) { fprintf(stderr, "cannot read %s/%s\n", dir.c_str(), f.name); return false; }
        for (int r = 0; r < f.reloads; r++)
            for (uint32_t i = 0; i < f.length; i += 2)   // ROM_GROUPWORD | ROM_SKIP(2): 2 bytes, skip 2
            {
                uint32_t dst = f.offset + 2 * f.length * r + i * 2;
                rom[dst] = data[i];
                rom[dst + 1] = data[i + 1];
            }
    }
    return true;
}

bool load_io(const std::string& path, std::vector<IoEntry>& io)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        IoEntry e{};
        char a[32];
        unsigned d, m;
        if (sscanf(line, "%" SCNu64 " %" SCNu64 " %31s %x %x", &e.sec, &e.atto, a, &d, &m) != 5) continue;
        e.write = (a[0] == 'W');
        e.addr = uint32_t(strtoul(a + (e.write ? 1 : 0), nullptr, 16));
        e.data = d;
        e.mask = m;
        io.push_back(e);
    }
    fclose(f);
    return true;
}

bool load_trace(const std::string& path, std::vector<TraceEntry>& tr)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    static char line[1024];
    size_t ln = 0;
    while (fgets(line, sizeof(line), f))
    {
        ln++;
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        TraceEntry e{};
        e.line = ln;
        unsigned pc, irq;
        if (sscanf(line, "   (interrupted at %x, IRQ %u)", &pc, &irq) == 2)
        {
            e.marker = true; e.pc = pc; e.irq = int(irq);
            tr.push_back(e);
            continue;
        }
        char* colon = strstr(line, ": ");
        if (!colon) continue;
        unsigned long long cyc;
        if (sscanf(line, "%llx %x:", &cyc, &pc) != 2) continue;
        e.cycles = cyc; e.pc = pc; e.text = colon + 2;
        tr.push_back(e);
    }
    fclose(f);
    return true;
}

class TestBoard : public V60Bus
{
public:
    std::vector<uint8_t> rom, ram;
    const std::vector<IoEntry>* io = nullptr;
    size_t io_pos = 0;
    V60Cpu* cpu = nullptr;

    // first replay problem
    bool        io_fail = false;
    std::string io_fail_msg;
    size_t      time_mismatches = 0;
    std::string first_time_mismatch;
    size_t      unlogged = 0;     // accesses outside ROM/RAM and outside the logged range
    size_t      fetches_logged = 0;

    // interrupt handling seen by the core
    int      irq_taken = 0;
    uint32_t irq_taken_pc = 0;
    int      irq_taken_line = -1;

    // segas32.cpp interrupt controller (m_v60_irq_control), device_reset: all 0xff
    uint8_t ctl[16];
    // queued line changes (MAME's set_input_line* from a handler go through the event queue)
    struct LineEvent { int state, vector; };
    std::vector<LineEvent> events;
    bool queue_events = true;

    TestBoard() { memset(ctl, 0xff, sizeof(ctl)); }

    uint32_t peek32(uint32_t a) const
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= uint32_t(peek8(a + i)) << (8 * i);
        return v;
    }
    uint8_t peek8(uint32_t a) const
    {
        a &= 0xffffff;
        if (a < 0x200000) return rom[a];
        if (a < 0x300000) return ram[a & 0x1ffff];
        if (a >= 0xf00000) return rom[a - 0xf00000];
        return 0;
    }

    // update_irq_state
    void update_irq_state()
    {
        uint8_t effirq = ctl[7] & ~ctl[6] & 0x1f;
        int vector;
        for (vector = 0; vector < 5; vector++)
            if (effirq & (1 << vector))
            {
                set_line(V60Cpu::ASSERT_LINE, vector);
                break;
            }
        if (vector == 5)
            set_line(V60Cpu::CLEAR_LINE, -1);
    }
    void set_line(int state, int vector)
    {
        if (queue_events)
        {
            events.push_back({ state, vector });
            cpu->abort_timeslice();   // the scheduler's synchronize() ends the timeslice
        }
        else if (state == V60Cpu::CLEAR_LINE)
            cpu->set_input_line(0, V60Cpu::CLEAR_LINE);
        else
            cpu->set_input_line_and_vector(0, state, vector);
    }
    void apply_events()
    {
        // empty_event_queue: all queued events in order
        std::vector<LineEvent> ev;
        ev.swap(events);
        for (auto& e : ev)
            if (e.state == V60Cpu::CLEAR_LINE)
                cpu->set_input_line(0, V60Cpu::CLEAR_LINE);
            else
                cpu->set_input_line_and_vector(0, e.state, e.vector);
    }

    // int_control_w (8-bit handler on the 32-bit bus: one call per selected byte lane)
    void int_control_w(uint32_t offset, uint8_t data)
    {
        switch (offset)
        {
            case 0: case 1: case 2: case 3: case 4: ctl[offset] = data; break;  // vectors
            case 5: ctl[offset] = data; break;                                  // unknown
            case 6: ctl[offset] = data; update_irq_state(); break;              // mask
            case 7: ctl[offset] &= data; update_irq_state(); break;             // acknowledge
            case 8: case 9: case 10: case 11: ctl[offset] = data; break;        // timers (not modelled)
            default: break;                                                     // sound IRQ (not modelled)
        }
    }

    // inject one interrupt source mapped to 'vector' (signal_v60_irq's effect)
    void signal_vector(int vector)
    {
        ctl[7] |= 1 << vector;
        update_irq_state();
    }

    const IoEntry* replay(bool write, uint32_t a, uint32_t data, uint32_t mask)
    {
        if (io_pos >= io->size())
        {
            if (!io_fail) { io_fail = true; char b[160]; snprintf(b, sizeof b, "access beyond the end of main.io: %s %06X data %08X mask %08X", write ? "W" : "R", a, data, mask); io_fail_msg = b; }
            return nullptr;
        }
        const IoEntry& e = (*io)[io_pos++];
        if (!io_fail && (e.write != write || e.addr != a || e.mask != mask || (write && e.data != data)))
        {
            io_fail = true;
            char b[256];
            snprintf(b, sizeof b, "main.io entry %zu: log %s %06X data %08X mask %08X, core %s %06X data %08X mask %08X",
                     io_pos, e.write ? "W" : "R", e.addr, e.data, e.mask, write ? "W" : "R", a, data, mask);
            io_fail_msg = b;
        }
        // MAME's time of the access in CPU cycles (20 MHz: 5e10 attoseconds per cycle)
        uint64_t t = e.sec * 20000000ull + e.atto / 50000000000ull;
        if (t != cpu->total_cycles() || e.atto % 50000000000ull)
        {
            if (!time_mismatches++)
            {
                char b[160];
                snprintf(b, sizeof b, "main.io entry %zu: logged at cycle %" PRIu64 ", core at %" PRIu64, io_pos, t, cpu->total_cycles());
                first_time_mismatch = b;
            }
        }
        return &e;
    }

    uint32_t read_common(uint32_t addr, uint32_t mem_mask, bool fetch)
    {
        uint32_t a = addr & 0xffffff;   // multi32_map global_mask
        if (a < 0x200000) return peek32(a);
        if (a < 0x300000) { uint32_t v; memcpy(&v, &ram[a & 0x1ffff], 4); return v; }
        if (a >= 0xf00000) return peek32(a);
        if (fetch) fetches_logged++;
        const IoEntry* e = replay(false, a, 0, mem_mask);
        return e ? e->data : 0xffffffff;
    }
    uint32_t read32(uint32_t addr, uint32_t mem_mask) override { return read_common(addr, mem_mask, false); }
    uint32_t fetch32(uint32_t addr, uint32_t mem_mask) override { return read_common(addr, mem_mask, true); }

    void write32(uint32_t addr, uint32_t data, uint32_t mem_mask) override
    {
        uint32_t a = addr & 0xffffff;
        if (a < 0x200000 || a >= 0xf00000) return;   // ROM
        if (a < 0x300000)
        {
            uint32_t v;
            memcpy(&v, &ram[a & 0x1ffff], 4);
            v = (v & ~mem_mask) | (data & mem_mask);
            memcpy(&ram[a & 0x1ffff], &v, 4);
            return;
        }
        replay(true, a, data, mem_mask);
        if ((a & 0xf80000) == 0xd00000)   // int_control, $D00000-$D0000F mirror $07FFF0
            for (int lane = 0; lane < 4; lane++)
                if ((mem_mask >> (8 * lane)) & 0xff)
                    int_control_w((a & 0xc) + lane, uint8_t(data >> (8 * lane)));
    }

    int irq_acknowledge(int irqline, int vector) override { return vector; }
    void debug_interrupt_hook(int irqline, uint32_t pc) override
    {
        irq_taken++;
        irq_taken_pc = pc;
        irq_taken_line = irqline;
    }
};

// vector (0..4) whose table entry is the handler address
int vector_for_handler(const TestBoard& b, V60Cpu& cpu, uint32_t handler, int* count)
{
    uint32_t base = cpu.reg(41) & ~0xfffu;   // SBR, GETINTVECT
    int found = -1;
    *count = 0;
    for (int v = 0; v < 5; v++)
        if (b.peek32(base + (v + 0x40) * 4) == handler)
        {
            if (found < 0) found = v;
            (*count)++;
        }
    return found;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: v60test main.bin main.tr main.io [-o trace] [-romdir dir] [-n count] [-bench]\n");
        return 2;
    }
    std::string outpath, romdir;
    size_t limit = SIZE_MAX;
    bool bench = false;
    for (int i = 4; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) outpath = argv[++i];
        else if (a == "-romdir" && i + 1 < argc) romdir = argv[++i];
        else if (a == "-n" && i + 1 < argc) limit = strtoull(argv[++i], nullptr, 10);
        else if (a == "-bench") bench = true;
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }

    std::vector<uint8_t> rom;
    if (!read_file(argv[1], rom) || rom.size() != 0x200000) { fprintf(stderr, "cannot read %s (2 MB)\n", argv[1]); return 1; }
    if (!romdir.empty())
    {
        std::vector<uint8_t> built;
        if (!build_rom(romdir, built)) return 1;
        size_t diff = 0;
        for (size_t i = 0; i < rom.size(); i++) diff += rom[i] != built[i];
        printf("ROM check: main.bin vs ROM_LOAD32_WORD_x4/ROM_LOAD32_WORD layout from %s: %zu bytes differ%s\n",
               romdir.c_str(), diff, diff ? " (using the rebuilt image)" : "");
        if (diff) rom = built;
    }
    std::vector<IoEntry> io;
    std::vector<TraceEntry> tr;
    if (!load_io(argv[3], io)) { fprintf(stderr, "cannot read %s\n", argv[3]); return 1; }
    if (!load_trace(argv[2], tr)) { fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    size_t ninstr = 0, nmark = 0;
    for (auto& e : tr) (e.marker ? nmark : ninstr)++;
    printf("main.tr: %zu instructions, %zu interrupt markers; main.io: %zu accesses\n", ninstr, nmark, io.size());

    TestBoard board;
    board.rom = rom;
    board.ram.assign(0x20000, 0);   // MAME zero-fills RAM shares
    board.io = &io;
    V60Cpu cpu(board);
    board.cpu = &cpu;
    cpu.set_direct_decode_mask(0xffffff);
    cpu.map_direct(0x000000, 0x1fffff, board.rom.data(), nullptr);
    cpu.map_direct(0xf00000, 0xffffff, board.rom.data(), nullptr);   // region offset 0 (first 1 MB)
    for (uint32_t m = 0x200000; m < 0x300000; m += 0x20000)
        cpu.map_direct(m, m + 0x1ffff, board.ram.data(), board.ram.data());
    cpu.reset();
    cpu.set_input_line(0, V60Cpu::CLEAR_LINE);   // segas32_state::device_reset

    V60Disassembler dasm;
    auto peek = [&](uint32_t a) -> uint8_t { return board.peek8(a); };
    FILE* out = outpath.empty() ? nullptr : fopen(outpath.c_str(), "w");

    // The first instruction (the reset PC) is not in main.tr; it runs at cycle 0.
    {
        uint32_t pc0 = cpu.pc();
        std::string t = dasm.disassemble(pc0, peek);
        printf("first instruction (not in main.tr): %X %08X: %s\n", 0u, pc0, t.c_str());
        if (out) fprintf(out, "%X %08X: %s\n", 0u, pc0, t.c_str());
        cpu.run(8);
        board.apply_events();
    }

    size_t matched = 0, text_equal = 0, markers_ok = 0, auto_taken = 0, injected = 0;
    std::string divergence;
    int taken_seen = board.irq_taken;
    std::vector<std::pair<size_t, int>> inject_points;   // (instructions executed before, vector) for -bench
    size_t executed = 1;
    uint64_t last_cycles = 0;
    auto t0 = std::chrono::steady_clock::now();

    for (size_t k = 0; k < tr.size() && matched < limit; k++)
    {
        const TraceEntry& e = tr[k];
        if (e.marker)
        {
            if (board.irq_taken > taken_seen)
            {
                // the core took it by itself (line still asserted)
                if (board.irq_taken_pc != e.pc || board.irq_taken_line != e.irq)
                {
                    char b[200]; snprintf(b, sizeof b, "main.tr line %zu: interrupt at %08X IRQ %d, core took one at %08X IRQ %d", e.line, e.pc, e.irq, board.irq_taken_pc, board.irq_taken_line);
                    divergence = b; break;
                }
                taken_seen = board.irq_taken;
                auto_taken++;
                markers_ok++;
                inject_points.push_back({ executed, -1 });
                continue;
            }
            // raise the source: the vector is the one whose table entry is the next line's PC
            size_t n = k + 1;
            while (n < tr.size() && tr[n].marker) n++;
            if (n >= tr.size()) break;
            int count = 0;
            int v = vector_for_handler(board, cpu, tr[n].pc, &count);
            if (v < 0)
            {
                char b[200]; snprintf(b, sizeof b, "main.tr line %zu: no vector 0-4 leads to handler %08X (SBR %08X)", e.line, tr[n].pc, cpu.reg(41));
                divergence = b; break;
            }
            if (count > 1) printf("note: main.tr line %zu: %d vectors lead to %08X, using %d\n", e.line, count, tr[n].pc, v);
            if (cpu.pc() != e.pc)
            {
                char b[200]; snprintf(b, sizeof b, "main.tr line %zu: interrupt at %08X but the core is at %08X", e.line, e.pc, cpu.pc());
                divergence = b; break;
            }
            board.queue_events = false;
            board.signal_vector(v);
            board.queue_events = true;
            if (board.irq_taken == taken_seen || board.irq_taken_pc != e.pc)
            {
                char b[240]; snprintf(b, sizeof b, "main.tr line %zu: raising vector %d (ctl mask %02X pending %02X) did not make the core take an interrupt at %08X (PSW %08X)",
                                      e.line, v, board.ctl[6], board.ctl[7], e.pc, cpu.psw());
                divergence = b; break;
            }
            taken_seen = board.irq_taken;
            injected++;
            markers_ok++;
            inject_points.push_back({ executed, v });
            continue;
        }

        if (board.irq_taken > taken_seen)
        {
            char b[200]; snprintf(b, sizeof b, "main.tr line %zu: the core took an interrupt at %08X that MAME did not take there", e.line, board.irq_taken_pc);
            divergence = b; break;
        }
        uint64_t cyc = cpu.total_cycles();
        uint32_t pc = cpu.pc();
        std::string text = dasm.disassemble(pc, peek);
        if (out) fprintf(out, "%" PRIX64 " %08X: %s\n", cyc, pc, text.c_str());
        if (pc != e.pc || cyc != e.cycles)
        {
            char b[300]; snprintf(b, sizeof b, "main.tr line %zu: MAME %" PRIX64 " %08X: %s | core %" PRIX64 " %08X: %s", e.line, e.cycles, e.pc, e.text.c_str(), cyc, pc, text.c_str());
            divergence = b; break;
        }
        if (text == e.text) text_equal++;
        else if (text_equal == matched)
            printf("first disassembly difference, main.tr line %zu: MAME \"%s\" core \"%s\"\n", e.line, e.text.c_str(), text.c_str());
        matched++;
        last_cycles = cyc;

        try { cpu.run(8); }
        catch (const V60Cpu::FatalError& err) { divergence = std::string("core fatalerror: ") + err.what(); break; }
        executed++;
        if (cpu.ppc() != pc)
        {
            char b[200]; snprintf(b, sizeof b, "main.tr line %zu: the core ran %08X instead of %08X", e.line, cpu.ppc(), pc);
            divergence = b; break;
        }
        board.apply_events();
        if (board.io_fail) { divergence = "bus replay: " + board.io_fail_msg + " (during main.tr line " + std::to_string(e.line) + ")"; break; }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (out) fclose(out);

    printf("\ntrace comparison (instruction lines of main.tr, the reset instruction excluded):\n");
    printf("  matched %zu of %zu instructions (PC and totalcycles), last matched cycle %" PRIX64 "\n", matched, ninstr, last_cycles);
    printf("  disassembly text equal on %zu of %zu matched lines\n", text_equal, matched);
    printf("  interrupt markers: %zu of %zu reproduced (%zu taken by the core on its own, %zu raised by the test)\n", markers_ok, nmark, auto_taken, injected);
    printf("  main.io: %zu of %zu accesses replayed%s%s\n", board.io_pos, io.size(), board.io_fail ? ", FIRST MISMATCH: " : ", all matching address/mask/data",
           board.io_fail ? board.io_fail_msg.c_str() : "");
    printf("  main.io times: %zu accesses at a cycle other than the core's total_cycles()%s%s\n", board.time_mismatches,
           board.time_mismatches ? ", first: " : "", board.first_time_mismatch.c_str());
    printf("  opcode fetches through the logged range: %zu\n", board.fetches_logged);
    printf("  %s\n", divergence.empty() ? "no divergence" : ("DIVERGENCE: " + divergence).c_str());
    printf("  (%.2f s with tracing and disassembly)\n", secs);

    if (bench && divergence.empty())
    {
        // Same span without hooks or disassembly: timeslices up to the next interrupt point,
        // ended early by interrupt-controller writes (abort_timeslice) as a board would do it.
        TestBoard b2;
        b2.rom = rom;
        b2.ram.assign(0x20000, 0);
        b2.io = &io;
        V60Cpu c2(b2);
        b2.cpu = &c2;
        c2.set_direct_decode_mask(0xffffff);
        c2.map_direct(0x000000, 0x1fffff, b2.rom.data(), nullptr);
        c2.map_direct(0xf00000, 0xffffff, b2.rom.data(), nullptr);
        for (uint32_t m = 0x200000; m < 0x300000; m += 0x20000)
            c2.map_direct(m, m + 0x1ffff, b2.ram.data(), b2.ram.data());
        c2.reset();
        auto t1 = std::chrono::steady_clock::now();
        uint64_t end_instr = executed;
        size_t ip = 0;
        int taken_handled = 0;
        while (c2.total_cycles() < end_instr * 8)
        {
            uint64_t target = end_instr * 8;
            if (ip < inject_points.size()) target = std::min<uint64_t>(target, inject_points[ip].first * 8);
            while (c2.total_cycles() < target)
            {
                c2.run(int(target - c2.total_cycles()));
                b2.apply_events();
            }
            while (ip < inject_points.size() && inject_points[ip].first * 8 == c2.total_cycles())
            {
                if (inject_points[ip].second >= 0 && b2.irq_taken == taken_handled)
                {
                    b2.queue_events = false;
                    b2.signal_vector(inject_points[ip].second);
                    b2.queue_events = true;
                }
                taken_handled = b2.irq_taken;
                ip++;
            }
        }
        double s2 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        printf("\nbench: %" PRIu64 " instructions (%.3f s of V70 time) in %.3f s = %.1f M instructions/s; final PC %08X %s; main.io %s, %zu of %zu replayed\n",
               end_instr, double(end_instr * 8) / 20e6, s2, double(end_instr) / s2 / 1e6, c2.pc(),
               c2.pc() == cpu.pc() ? "(same as traced run)" : "(DIFFERS from traced run)",
               b2.io_fail ? "MISMATCH" : "ok", b2.io_pos, io.size());
    }
    return divergence.empty() ? 0 : 1;
}
