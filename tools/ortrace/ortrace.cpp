// ortrace: experiments on OutRunners' program (MAME set orunnersj) on the emulated System Multi 32
// board (crossover/orboard), for the reverse engineering in docs/OUTRUNNERS-RE.md.
//
// The board is run from power-on (fresh EEPROM unless -eeprom) and a script is applied at each
// VBLANK. Frames are numbered as in tools/orbtest (MAME's Lua frame f: f = n + 1 at the n-th
// VBLANK), so orbtest's .sched files are valid scripts.
//
// usage:
//   ortrace run <romdir> <script> [-eeprom file] [-draw mask] [-o out.txt]
//   ortrace dasm <romdir> <start> <end>        linear disassembly of the program ROM
//   ortrace list <romdir> <cov file> [lo hi]   listing of the executed instructions of a coverage file
//
// Script: one command per line, "<frame> <command>" or "<f1>-<f2> <command>" (every frame of the
// range); '#' starts a comment; "include file" reads another script (relative to this one).
// Commands run at the frame's VBLANK begin (the CPU is between timeslices), in file order:
//   set PORT mask 0|1          digital input (active low), as orbtest (PORT = P1_A, SERVICE12_A, ...)
//   aset PORT mask value       8-bit field (ANALOG1..8)
//   w8|w16|w32 addr value      write work RAM / shared RAM (little-endian)
//   p8|p16|p32|s8|s16|s32 addr [addr...]   print unsigned / signed values: "f <frame> <addr>=<value> ..."
//   mem addr len               hex dump
//   snap file.png [screen]     the last drawn frame of screen 0 (left, monitor A) or 1
//   dump file                  work RAM (128 KB, address order)
//   dumpapp file               work RAM appended to file (one 128 KB record per call)
//   watch addr len [max]       report every change of these bytes with the PC of the instruction
//                              that made it ("W f=.. pc=.. addr=.. old->new"; at most max reports)
//   vwatch lo hi [max]         report CPU writes to bus addresses lo..hi outside work RAM (video RAM,
//                              sprite RAM, palette, I/O) with the PC and R20 (the object being run):
//                              "V f=.. pc=.. addr=.. data=.. mask=.. r20=.."
//   unwatch                    remove all watches
//   bp pc [max] [mem addr len] print the registers each time pc is about to execute (at most max);
//                              "unbp" removes them
//   Numbers: decimal, $hex or 0xhex; negative values are written as their two's complement.
//   pcw8|pcw16|pcw32 pc addr value   from now on, write value to addr each time the instruction at pc
//                              is about to execute (a hook in the middle of the game's tick); "unpcw" ends them
//   pcdump pc file [max]       append work RAM to file when the instruction at pc is reached (max times, default 1)
//   cov file                   start counting executed instructions (per PC); "covend" writes
//                              "<pc> <count>" for every executed PC and every call target as "C <pc> <count>"
//   covend
//   trace file [regs]          instruction trace (frame, PC, disassembly; with "regs" also r0-r28 and sp,
//                              before the instruction) to file
//   traceend
//   calls                      start counting call targets (BSR/CALL/JSR); "callsend" prints the counts
//   callsend
//   draw mask                  screens to draw from now on
//   chips file                 every YM3438 / MultiPCM write of the sound board from now on (ORSound::chip_log)
//   audio file                 the sound board's left-speaker streams from now on (file.ym, file.pcm: floats)
//   exit
#include "crossover/orboard.hpp"
#include "crossover/v60/v60d.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <png.h>

namespace
{

struct Cmd { int f1, f2; std::vector<std::string> a; };

uint32_t num(const std::string& s) { return (uint32_t) strtoul(s.c_str() + (s[0] == '$' ? 1 : 0), nullptr, s[0] == '$' ? 16 : 0); }

std::vector<Cmd> load_script(const std::string& path)
{
    std::vector<Cmd> v;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { fprintf(stderr, "cannot read %s\n", path.c_str()); exit(1); }
    char line[1024];
    while (fgets(line, sizeof line, f))
    {
        if (char* c = strchr(line, '#')) *c = 0;
        std::vector<std::string> tok;
        for (char* t = strtok(line, " \t\r\n"); t; t = strtok(nullptr, " \t\r\n")) tok.push_back(t);
        if (tok.size() == 2 && tok[0] == "include")
        {
            // "include file": another script, relative to this one's folder
            size_t sl = path.find_last_of('/');
            std::string inc = (tok[1][0] == '/' || sl == std::string::npos) ? tok[1] : path.substr(0, sl + 1) + tok[1];
            std::vector<Cmd> sub = load_script(inc);
            v.insert(v.end(), sub.begin(), sub.end());
            continue;
        }
        if (tok.size() < 2) continue;
        Cmd c;
        size_t dash = tok[0].find('-');
        if (dash != std::string::npos) { c.f1 = atoi(tok[0].c_str()); c.f2 = atoi(tok[0].c_str() + dash + 1); }
        else c.f1 = c.f2 = atoi(tok[0].c_str());
        c.a.assign(tok.begin() + 1, tok.end());
        v.push_back(c);
    }
    fclose(f);
    return v;
}

bool write_png(const std::string& path, const uint32_t* px, int w, int h, int stride)
{
    std::vector<uint8_t> buf((size_t) w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            uint32_t p = px[(size_t) y * stride + x];
            uint8_t* o = &buf[((size_t) y * w + x) * 3];
            o[0] = (uint8_t)(p >> 16); o[1] = (uint8_t)(p >> 8); o[2] = (uint8_t) p;
        }
    png_image img;
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    img.width = (png_uint_32) w; img.height = (png_uint_32) h; img.format = PNG_FORMAT_RGB;
    return png_image_write_to_file(&img, path.c_str(), 0, buf.data(), 0, nullptr) != 0;
}

// The board with the V70's instruction hook
class TraceBoard : public ORBoard
{
public:
    FILE* out = stdout;
    uint32_t cur_frame = 0;
    V60Disassembler dis;

    // coverage
    std::vector<uint32_t> cov;              // per PC in $000000-$1FFFFF
    std::map<uint32_t, uint32_t> call_cov;  // call targets
    bool cov_on = false;
    // per-frame call counting
    bool calls_on = false;
    std::map<uint32_t, uint32_t> calls;
    // watches
    struct Watch { uint32_t addr, len, max, n; std::vector<uint8_t> last; };
    std::vector<Watch> watches;
    // breakpoints
    struct Bp { uint32_t pc, max, n, maddr, mlen; };
    std::vector<Bp> bps;
    // writes made when an instruction is reached (before it executes)
    struct PcWrite { uint32_t pc, addr, value; int size; };
    std::vector<PcWrite> pc_writes;
    // work RAM dumps made when an instruction is reached (appended, at most max times)
    struct PcDump { uint32_t pc, max, n; std::string file; };
    std::vector<PcDump> pc_dumps;
    // trace
    FILE* tr = nullptr;
    bool tr_regs = false;

    uint32_t prev_pc = 0;
    bool prev_call = false;

    // bus writes outside work RAM (video, sprite RAM, I/O): "V f=.. pc=.. addr=.. data=.. mask=.."
    struct BusWatch { uint32_t lo, hi, max, n; };
    std::vector<BusWatch> bus_watches;
    void write32(uint32_t addr, uint32_t data, uint32_t mask) override
    {
        for (BusWatch& w : bus_watches)
            if (addr + 3 >= w.lo && addr <= w.hi && w.n < w.max)
            {
                w.n++;
                fprintf(out, "V f=%u pc=%06x addr=%06x data=%08x mask=%08x r20=%06x\n", cur_frame, prev_pc, addr, data, mask, maincpu().reg(20));
            }
        ORBoard::write32(addr, data, mask);
    }

    void update_hook()
    {
        maincpu().set_instruction_hook(cov_on || calls_on || !watches.empty() || !bps.empty() || tr || !bus_watches.empty() || !pc_writes.empty() || !pc_dumps.empty());
    }

    std::string disasm(uint32_t pc, uint32_t* len = nullptr)
    {
        uint32_t r = 0;
        std::string s = dis.disassemble(pc, [this](uint32_t a) { return read8(a); }, &r);
        if (len) *len = r & V60Disassembler::LENGTHMASK;
        return s;
    }

    void regs(FILE* f)
    {
        V60Cpu& c = maincpu();
        for (int i = 0; i < 29; i++) fprintf(f, "r%d=%08x%s", i, c.reg(i), (i % 8 == 7) ? "\n  " : " ");
        fprintf(f, "ap=%08x fp=%08x sp=%08x psw=%08x\n", c.reg(29), c.reg(30), c.reg(31), c.reg(33));
    }

    void debug_instruction_hook(uint32_t pc) override
    {
        if (!watches.empty())
            for (Watch& w : watches)
                for (uint32_t i = 0; i < w.len; i++)
                {
                    uint8_t v = read8(w.addr + i);
                    if (v != w.last[i])
                    {
                        if (w.n < w.max)
                            fprintf(out, "W f=%u pc=%06x addr=%06x %02x->%02x\n", cur_frame, prev_pc, w.addr + i, w.last[i], v);
                        w.n++;
                        w.last[i] = v;
                    }
                }
        uint8_t op = read8(pc);
        bool is_call = op == 0x48 || op == 0x49 || op == 0xe8 || op == 0xe9;
        if (prev_call)
        {
            if (cov_on) call_cov[pc]++;
            if (calls_on) calls[pc]++;
        }
        if (cov_on && pc < 0x200000) cov[pc]++;
        for (PcWrite& w : pc_writes)
            if (w.pc == pc)
                for (int i = 0; i < w.size; i++) write8(w.addr + (uint32_t) i, (uint8_t)(w.value >> (8 * i)));
        for (PcDump& d : pc_dumps)
            if (d.pc == pc && d.n < d.max)
            {
                d.n++;
                if (FILE* f = fopen(d.file.c_str(), "ab")) { fwrite(work_ram, 1, sizeof work_ram, f); fclose(f); }
            }
        for (Bp& b : bps)
            if (b.pc == pc && b.n < b.max)
            {
                b.n++;
                fprintf(out, "B f=%u pc=%06x from=%06x\n  ", cur_frame, pc, prev_pc);
                regs(out);
                if (b.mlen)
                {
                    fprintf(out, "  mem %06x:", b.maddr);
                    for (uint32_t i = 0; i < b.mlen; i++) fprintf(out, " %02x", read8(b.maddr + i));
                    fprintf(out, "\n");
                }
            }
        if (tr)
        {
            fprintf(tr, "%u %06x: %s", cur_frame, pc, disasm(pc).c_str());
            if (tr_regs)
            {
                V60Cpu& c = maincpu();
                fprintf(tr, "  |");
                for (int i = 0; i < 29; i++) fprintf(tr, " %x", c.reg(i));
                fprintf(tr, " sp=%x", c.reg(31));
            }
            fputc('\n', tr);
        }
        prev_pc = pc;
        prev_call = is_call;
    }
};

uint8_t* port_byte(ORBoard& b, const std::string& port)
{
    ORBoard::Inputs& in = b.in;
    if (port == "P1_A") return &in.p1_a;
    if (port == "P2_A") return &in.p2_a;
    if (port == "PORTC_A") return &in.portc_a;
    if (port == "SERVICE12_A") return &in.service12_a;
    if (port == "SERVICE34_A") return &in.service34_a;
    if (port == "P1_B") return &in.p1_b;
    if (port == "P2_B") return &in.p2_b;
    if (port == "PORTC_B") return &in.portc_b;
    if (port == "SERVICE12_B") return &in.service12_b;
    if (port == "SERVICE34_B") return &in.service34_b;
    if (port.compare(0, 6, "ANALOG") == 0) return &in.analog[atoi(port.c_str() + 6) - 1];
    return nullptr;
}

uint32_t rd(ORBoard& b, uint32_t a, int size)
{
    uint32_t v = 0;
    for (int i = 0; i < size; i++) v |= (uint32_t) b.read8(a + (uint32_t) i) << (8 * i);
    return v;
}

void wr(ORBoard& b, uint32_t a, uint32_t v, int size)
{
    for (int i = 0; i < size; i++) b.write8(a + (uint32_t) i, (uint8_t)(v >> (8 * i)));
}

bool load_board(TraceBoard& b, const std::string& romdir)
{
    ORBoard::Roms roms; std::string err;
    if (!ORBoard::load_roms(romdir, roms, err)) { fprintf(stderr, "ROMs: %s\n", err.c_str()); return false; }
    if (!b.init(std::move(roms))) { fprintf(stderr, "board init failed\n"); return false; }
    return true;
}

int do_run(int argc, char** argv)
{
    std::string romdir = argv[2], script = argv[3], eepath, outpath;
    int draw = 3;
    for (int i = 4; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "-eeprom" && i + 1 < argc) eepath = argv[++i];
        else if (a == "-draw" && i + 1 < argc) draw = atoi(argv[++i]);
        else if (a == "-o" && i + 1 < argc) outpath = argv[++i];
    }
    std::vector<Cmd> cmds = load_script(script);
    static TraceBoard b;
    if (!load_board(b, romdir)) return 1;
    if (!outpath.empty()) b.out = fopen(outpath.c_str(), "w");
    std::vector<uint8_t> ee(128);
    if (!eepath.empty())
    {
        FILE* f = fopen(eepath.c_str(), "rb");
        if (!f || fread(ee.data(), 1, 128, f) != 128) { fprintf(stderr, "cannot read %s\n", eepath.c_str()); return 1; }
        fclose(f);
    }
    b.power_on(eepath.empty() ? nullptr : ee.data());
    b.draw_mask = (uint8_t) draw;
    bool done = false;
    std::string covpath;
    FILE* out = b.out;

    FILE* audio_ym = nullptr; FILE* audio_pcm = nullptr;
    auto at_frame = [&](int f)
    {
        b.cur_frame = (uint32_t) f;
        for (const Cmd& c : cmds)
        {
            if (f < c.f1 || f > c.f2) continue;
            const std::vector<std::string>& a = c.a;
            const std::string& k = a[0];
            if (k == "set" && a.size() >= 4)
            {
                uint8_t* p = port_byte(b, a[1]);
                uint32_t m = (uint32_t) strtoul(a[2].c_str(), nullptr, 16);
                if (p) *p = atoi(a[3].c_str()) ? (uint8_t)(*p & ~m) : (uint8_t)(*p | m);
            }
            else if (k == "aset" && a.size() >= 4)
            {
                uint8_t* p = port_byte(b, a[1]);
                if (p) *p = (uint8_t) atoi(a[3].c_str());
            }
            else if ((k == "w8" || k == "w16" || k == "w32") && a.size() >= 3)
                wr(b, num(a[1]), num(a[2]), k == "w8" ? 1 : k == "w16" ? 2 : 4);
            else if (k.size() >= 2 && (k[0] == 'p' || k[0] == 's') && isdigit((unsigned char) k[1]))
            {
                int size = atoi(k.c_str() + 1) / 8;
                fprintf(out, "f %d", f);
                for (size_t i = 1; i < a.size(); i++)
                {
                    uint32_t ad = num(a[i]), v = rd(b, ad, size);
                    if (k[0] == 's')
                    {
                        int32_t sv = size == 1 ? (int8_t) v : size == 2 ? (int16_t) v : (int32_t) v;
                        fprintf(out, " %06x=%d", ad, sv);
                    }
                    else fprintf(out, " %06x=%0*x", ad, size * 2, v);
                }
                fprintf(out, "\n");
            }
            else if (k == "mem" && a.size() >= 3)
            {
                uint32_t ad = num(a[1]), len = num(a[2]);
                for (uint32_t i = 0; i < len; i += 16)
                {
                    fprintf(out, "f %d %06x:", f, ad + i);
                    for (uint32_t j = i; j < std::min(len, i + 16); j++) fprintf(out, " %02x", b.read8(ad + j));
                    fprintf(out, "\n");
                }
            }
            else if (k == "snap" && a.size() >= 2)
            {
                int s = a.size() >= 3 ? atoi(a[2].c_str()) : 0;
                write_png(a[1], b.screen_pixels(s), b.screen_width(s), 224, S32Video::SCREEN_W_MAX);
            }
            else if ((k == "dump" || k == "dumpapp") && a.size() >= 2)
            {
                FILE* d = fopen(a[1].c_str(), k == "dump" ? "wb" : "ab");
                if (d) { fwrite(b.work_ram, 1, sizeof b.work_ram, d); fclose(d); }
            }
            else if (k == "watch" && a.size() >= 3)
            {
                TraceBoard::Watch w;
                w.addr = num(a[1]); w.len = num(a[2]); w.max = a.size() >= 4 ? num(a[3]) : 1000000; w.n = 0;
                for (uint32_t i = 0; i < w.len; i++) w.last.push_back(b.read8(w.addr + i));
                b.watches.push_back(w);
            }
            else if (k == "unwatch") { b.watches.clear(); b.bus_watches.clear(); }
            else if (k == "vwatch" && a.size() >= 3)
                b.bus_watches.push_back({ num(a[1]), num(a[2]), a.size() >= 4 ? num(a[3]) : 1000000, 0 });
            else if (k == "bp" && a.size() >= 2)
            {
                TraceBoard::Bp p{ num(a[1]), a.size() >= 3 ? num(a[2]) : 1000000, 0, 0, 0 };
                if (a.size() >= 6 && a[3] == "mem") { p.maddr = num(a[4]); p.mlen = num(a[5]); }
                b.bps.push_back(p);
            }
            else if (k == "unbp") b.bps.clear();
            else if ((k == "pcw8" || k == "pcw16" || k == "pcw32") && a.size() >= 4)
                b.pc_writes.push_back({ num(a[1]), num(a[2]), num(a[3]), k == "pcw8" ? 1 : k == "pcw16" ? 2 : 4 });
            else if (k == "unpcw") b.pc_writes.clear();
            else if (k == "pcdump" && a.size() >= 3)
                b.pc_dumps.push_back({ num(a[1]), a.size() >= 4 ? num(a[3]) : 1, 0, a[2] });
            else if (k == "cov" && a.size() >= 2)
            {
                covpath = a[1];
                b.cov.assign(0x200000, 0);
                b.call_cov.clear();
                b.cov_on = true;
            }
            else if (k == "covend" && b.cov_on)
            {
                FILE* cf = fopen(covpath.c_str(), "w");
                for (uint32_t pc = 0; pc < 0x200000; pc++) if (b.cov[pc]) fprintf(cf, "%06x %u\n", pc, b.cov[pc]);
                for (auto& e : b.call_cov) fprintf(cf, "C %06x %u\n", e.first, e.second);
                fclose(cf);
                b.cov_on = false;
            }
            else if (k == "trace" && a.size() >= 2)
            {
                b.tr = fopen(a[1].c_str(), "w");
                b.tr_regs = a.size() >= 3 && a[2] == "regs";
            }
            else if (k == "traceend" && b.tr) { fclose(b.tr); b.tr = nullptr; }
            else if (k == "calls") { b.calls.clear(); b.calls_on = true; }
            else if (k == "callsend")
            {
                fprintf(out, "calls up to frame %d:\n", f);
                for (auto& e : b.calls) fprintf(out, "  %06x %u\n", e.first, e.second);
                b.calls_on = false;
            }
            else if (k == "draw" && a.size() >= 2) b.draw_mask = (uint8_t) atoi(a[1].c_str());
            else if (k == "chips" && a.size() >= 2) b.sound().chip_log = fopen(a[1].c_str(), "w");
            else if (k == "audio" && a.size() >= 2)
            {
                // the sound board's left-speaker streams from now on, raw floats (MAME's scale): <file>.ym
                // (the YM3438's output 1, 55,930 Hz) and <file>.pcm (the MultiPCM's output 1, 44,642 Hz)
                b.sound().ym0.clear(); b.sound().ym1.clear(); b.sound().pcm0.clear(); b.sound().pcm1.clear();
                audio_ym = fopen((a[1] + ".ym").c_str(), "wb"); audio_pcm = fopen((a[1] + ".pcm").c_str(), "wb");
            }
            else if (k == "exit") done = true;
        }
        b.update_hook();
        if (audio_ym)
        {
            b.sound().flush_streams();
            fwrite(b.sound().ym1.data(), sizeof(float), b.sound().ym1.size(), audio_ym);
            fwrite(b.sound().pcm1.data(), sizeof(float), b.sound().pcm1.size(), audio_pcm);
        }
        b.sound().ym0.clear(); b.sound().ym1.clear(); b.sound().pcm0.clear(); b.sound().pcm1.clear();
        fflush(out);
    };
    b.on_frame = [&](uint64_t n) { at_frame((int) n + 1); };
    at_frame(1);
    while (!done) b.run_frame();
    if (b.tr) fclose(b.tr);
    if (out != stdout) fclose(out);
    return 0;
}

int do_dasm(int argc, char** argv)
{
    if (argc < 5) return 2;
    static TraceBoard b;
    if (!load_board(b, argv[2])) return 1;
    uint32_t pc = num(argv[3]), end = num(argv[4]);
    while (pc < end)
    {
        uint32_t len = 0;
        std::string s = b.disasm(pc, &len);
        printf("%06x:", pc);
        for (uint32_t i = 0; i < len && i < 8; i++) printf(" %02x", b.read8(pc + i));
        printf("%*s %s\n", (int)(3 * (8 - std::min<uint32_t>(len, 8))), "", s.c_str());
        pc += len ? len : 1;
    }
    return 0;
}

int do_list(int argc, char** argv)
{
    if (argc < 4) return 2;
    static TraceBoard b;
    if (!load_board(b, argv[2])) return 1;
    uint32_t lo = argc >= 6 ? num(argv[4]) : 0, hi = argc >= 6 ? num(argv[5]) : 0x200000;
    std::map<uint32_t, uint32_t> pcs, calls;
    FILE* f = fopen(argv[3], "r");
    if (!f) return 1;
    char line[128];
    while (fgets(line, sizeof line, f))
    {
        unsigned a, n;
        if (line[0] == 'C' && sscanf(line + 2, "%x %u", &a, &n) == 2) calls[a] = n;
        else if (sscanf(line, "%x %u", &a, &n) == 2) pcs[a] = n;
    }
    fclose(f);
    uint32_t next = 0;
    for (auto& e : pcs)
    {
        uint32_t pc = e.first;
        if (pc < lo || pc >= hi) continue;
        if (pc != next) printf("\n");
        auto c = calls.find(pc);
        if (c != calls.end()) printf("sub_%06x:                                   ; called %u\n", pc, c->second);
        uint32_t len = 0;
        std::string s = b.disasm(pc, &len);
        printf("%06x: %-40s ; %u\n", pc, s.c_str(), e.second);
        next = pc + len;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 4 && !strcmp(argv[1], "run")) return do_run(argc, argv);
    if (argc >= 5 && !strcmp(argv[1], "dasm")) return do_dasm(argc, argv);
    if (argc >= 4 && !strcmp(argv[1], "list")) return do_list(argc, argv);
    fprintf(stderr, "usage: ortrace run <romdir> <script> [-eeprom f] [-draw mask] [-o out]\n"
                    "       ortrace dasm <romdir> <start> <end>\n"
                    "       ortrace list <romdir> <cov file> [lo hi]\n");
    return 2;
}
