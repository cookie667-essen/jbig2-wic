#include "jbig2_core.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const auto sz = f.tellg();
    if (sz < 0) return false;
    out.resize(static_cast<size_t>(sz));
    f.seekg(0);
    return sz == 0 || static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), sz));
}

bool WriteFile(const std::string& path, const uint8_t* data, size_t size) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    return size == 0 || static_cast<bool>(f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size)));
}

bool EndsWithCI(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    const size_t off = s.size() - suf.size();
    for (size_t i = 0; i < suf.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[off + i])) !=
            std::tolower(static_cast<unsigned char>(suf[i]))) return false;
    }
    return true;
}

// --- Zero-Dependency PNG Writer (uncompressed DEFLATE stored blocks, 100% standard) ---
uint32_t Crc32(const uint8_t* data, size_t len) {
    static std::array<uint32_t, 256> tbl{};
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tbl[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = tbl[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t Adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void PushBE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFFu));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFFu));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFFu));
    v.push_back(static_cast<uint8_t>(x & 0xFFu));
}

void PushChunk(std::vector<uint8_t>& png, const char type[4], const uint8_t* data, size_t len) {
    PushBE32(png, static_cast<uint32_t>(len));
    const size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    if (len > 0) png.insert(png.end(), data, data + len);
    PushBE32(png, Crc32(png.data() + start, 4 + len));
}

bool WritePngRaw(const std::string& path, uint32_t w, uint32_t h, uint8_t bit_depth,
                 const uint8_t* rows, uint32_t row_bytes, uint32_t stride) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (1u + row_bytes));
    for (uint32_t y = 0; y < h; ++y) {
        raw.push_back(0x00); // Filter: None
        const uint8_t* r = rows + static_cast<size_t>(y) * stride;
        raw.insert(raw.end(), r, r + row_bytes);
    }

    // Zlib stored stream
    std::vector<uint8_t> z;
    z.reserve(raw.size() + (raw.size() / 65535u + 1u) * 5u + 6u);
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size() || pos == 0) {
        const size_t n = std::min<size_t>(65535u, raw.size() - pos);
        const bool last = (pos + n >= raw.size());
        z.push_back(last ? 0x01 : 0x00);
        z.push_back(static_cast<uint8_t>(n & 0xFFu));
        z.push_back(static_cast<uint8_t>((n >> 8) & 0xFFu));
        const uint16_t nlen = static_cast<uint16_t>(~n);
        z.push_back(static_cast<uint8_t>(nlen & 0xFFu));
        z.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFFu));
        if (n > 0) z.insert(z.end(), raw.data() + pos, raw.data() + pos + n);
        pos += n;
        if (last) break;
    }
    PushBE32(z, Adler32(raw.data(), raw.size()));

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    PushBE32(ihdr, w);
    PushBE32(ihdr, h);
    ihdr.push_back(bit_depth);
    ihdr.push_back(0); // Grayscale
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    PushChunk(png, "IHDR", ihdr.data(), ihdr.size());
    PushChunk(png, "IDAT", z.data(), z.size());
    PushChunk(png, "IEND", nullptr, 0);
    return WriteFile(path, png.data(), png.size());
}

// --- P4 PBM Reader / Writer (PBM: 1=black,0=white <-> Bitmap1bpp: 1=white,0=black) ---
bool ReadPbmP4(const std::string& path, jbig2::Bitmap1bpp& out, double dpi) {
    std::vector<uint8_t> buf;
    if (!ReadFile(path, buf) || buf.size() < 4) return false;
    size_t i = 0;
    auto next_tok = [&](std::string& tok) -> bool {
        tok.clear();
        while (i < buf.size()) {
            if (buf[i] == '#') {
                while (i < buf.size() && buf[i] != '\n') ++i;
            } else if (std::isspace(buf[i])) {
                ++i;
            } else {
                break;
            }
        }
        while (i < buf.size() && !std::isspace(buf[i]) && buf[i] != '#') {
            tok.push_back(static_cast<char>(buf[i++]));
        }
        return !tok.empty();
    };
    std::string magic, sw, sh;
    if (!next_tok(magic) || magic != "P4" || !next_tok(sw) || !next_tok(sh)) return false;
    if (i < buf.size() && std::isspace(buf[i])) ++i;
    const uint32_t w = static_cast<uint32_t>(std::stoul(sw));
    const uint32_t h = static_cast<uint32_t>(std::stoul(sh));
    const uint32_t stride = (w + 7u) / 8u;
    const size_t need = static_cast<size_t>(stride) * h;
    if (w == 0 || h == 0 || buf.size() - i < need) return false;

    out.width = w;
    out.height = h;
    out.stride = stride;
    out.dpi_x = dpi;
    out.dpi_y = dpi;
    out.pixels.resize(need);
    for (size_t k = 0; k < need; ++k) out.pixels[k] = static_cast<uint8_t>(~buf[i + k]);
    return true;
}

bool WritePbmP4(const std::string& path, const jbig2::Bitmap1bpp& bmp) {
    std::string hdr = "P4\n" + std::to_string(bmp.width) + " " + std::to_string(bmp.height) + "\n";
    const uint32_t row_bytes = (bmp.width + 7u) / 8u;
    const uint8_t tail_mask = (bmp.width & 7u) ? static_cast<uint8_t>(0xFFu << (8u - (bmp.width & 7u))) : 0xFFu;
    std::vector<uint8_t> out(hdr.begin(), hdr.end());
    out.reserve(out.size() + static_cast<size_t>(row_bytes) * bmp.height);
    for (uint32_t y = 0; y < bmp.height; ++y) {
        const uint8_t* r = bmp.pixels.data() + static_cast<size_t>(y) * bmp.stride;
        for (uint32_t x = 0; x < row_bytes; ++x) {
            uint8_t b = static_cast<uint8_t>(~r[x]);
            if (x + 1 == row_bytes) b &= tail_mask;
            out.push_back(b);
        }
    }
    return WriteFile(path, out.data(), out.size());
}

// --- Minimal ISO 32000-1 PDF Writer (/Filter /JBIG2Decode + /JBIG2Globals) ---
bool WritePdfDocument(const std::string& path, const std::vector<jbig2::Bitmap1bpp>& pages,
                      const jbig2::PdfEncodedDocument& enc) {
    if (pages.size() != enc.pages.size() || pages.empty()) return false;
    std::vector<std::string> objs;
    const bool has_globals = !enc.globals.empty();
    const uint32_t globals_id = has_globals ? 3u : 0u;
    const uint32_t first_page_obj = has_globals ? 4u : 3u;
    const uint32_t n = static_cast<uint32_t>(pages.size());

    objs.resize(first_page_obj - 1u + n * 3u);
    objs[0] = "<< /Type /Catalog /Pages 2 0 R >>";

    std::ostringstream kids;
    kids << "<< /Type /Pages /Count " << n << " /Kids [";
    for (uint32_t i = 0; i < n; ++i) kids << " " << (first_page_obj + i * 3u) << " 0 R";
    kids << " ] >>";
    objs[1] = kids.str();

    if (has_globals) {
        std::string s = "<< /Length " + std::to_string(enc.globals.size()) + " >>\nstream\n";
        s.append(reinterpret_cast<const char*>(enc.globals.data()), enc.globals.size());
        s += "\nendstream";
        objs[globals_id - 1u] = std::move(s);
    }

    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t page_id = first_page_obj + i * 3u;
        const uint32_t content_id = page_id + 1u;
        const uint32_t img_id = page_id + 2u;
        const auto& pg = pages[i];
        const double w_pt = pg.width * 72.0 / (pg.dpi_x > 1.0 ? pg.dpi_x : 300.0);
        const double h_pt = pg.height * 72.0 / (pg.dpi_y > 1.0 ? pg.dpi_y : 300.0);

        std::ostringstream po;
        po << std::fixed << std::setprecision(2)
           << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << w_pt << " " << h_pt << "] "
           << "/Resources << /XObject << /Im0 " << img_id << " 0 R >> >> "
           << "/Contents " << content_id << " 0 R >>";
        objs[page_id - 1u] = po.str();

        std::ostringstream cs;
        cs << std::fixed << std::setprecision(2) << "q " << w_pt << " 0 0 " << h_pt << " 0 0 cm /Im0 Do Q";
        const std::string cstr = cs.str();
        objs[content_id - 1u] = "<< /Length " + std::to_string(cstr.size()) + " >>\nstream\n" + cstr + "\nendstream";

        std::ostringstream im;
        im << "<< /Type /XObject /Subtype /Image /Width " << pg.width << " /Height " << pg.height
           << " /ColorSpace /DeviceGray /BitsPerComponent 1 /Decode [1 0] /Filter /JBIG2Decode ";
        if (has_globals) im << "/DecodeParms << /JBIG2Globals " << globals_id << " 0 R >> ";
        im << "/Length " << enc.pages[i].size() << " >>\nstream\n";
        std::string ims = im.str();
        ims.append(reinterpret_cast<const char*>(enc.pages[i].data()), enc.pages[i].size());
        ims += "\nendstream";
        objs[img_id - 1u] = std::move(ims);
    }

    std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offs(objs.size() + 1u, 0);
    for (size_t i = 0; i < objs.size(); ++i) {
        offs[i + 1u] = pdf.size();
        pdf += std::to_string(i + 1u) + " 0 obj\n" + objs[i] + "\nendobj\n";
    }
    const size_t xref_pos = pdf.size();
    std::ostringstream xr;
    xr << "xref\n0 " << (objs.size() + 1u) << "\n0000000000 65535 f \n";
    for (size_t i = 1; i <= objs.size(); ++i) {
        xr << std::setw(10) << std::setfill('0') << offs[i] << " 00000 n \n";
    }
    xr << "trailer\n<< /Size " << (objs.size() + 1u) << " /Root 1 0 R >>\nstartxref\n" << xref_pos << "\n%%EOF\n";
    pdf += xr.str();
    return WriteFile(path, reinterpret_cast<const uint8_t*>(pdf.data()), pdf.size());
}

// --- Antialiased Area-Averaging Thumbnail (1bpp -> 8-bit Grayscale PNG) ---
bool WriteThumbnailPng(const std::string& path, const jbig2::Bitmap1bpp& bmp, uint32_t max_size) {
    if (bmp.width == 0 || bmp.height == 0 || max_size == 0) return false;
    const double scale = std::min(1.0, static_cast<double>(max_size) / std::max(bmp.width, bmp.height));
    const uint32_t dw = std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(bmp.width * scale)));
    const uint32_t dh = std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(bmp.height * scale)));
    std::vector<uint8_t> gray(static_cast<size_t>(dw) * dh, 255);

    for (uint32_t dy = 0; dy < dh; ++dy) {
        const uint32_t sy0 = static_cast<uint32_t>((static_cast<uint64_t>(dy) * bmp.height) / dh);
        const uint32_t sy1 = std::max(sy0 + 1u, static_cast<uint32_t>((static_cast<uint64_t>(dy + 1u) * bmp.height) / dh));
        for (uint32_t dx = 0; dx < dw; ++dx) {
            const uint32_t sx0 = static_cast<uint32_t>((static_cast<uint64_t>(dx) * bmp.width) / dw);
            const uint32_t sx1 = std::max(sx0 + 1u, static_cast<uint32_t>((static_cast<uint64_t>(dx + 1u) * bmp.width) / dw));
            uint32_t white = 0, total = 0;
            for (uint32_t sy = sy0; sy < sy1 && sy < bmp.height; ++sy) {
                const uint8_t* row = bmp.pixels.data() + static_cast<size_t>(sy) * bmp.stride;
                for (uint32_t sx = sx0; sx < sx1 && sx < bmp.width; ++sx) {
                    white += (row[sx >> 3] >> (7u - (sx & 7u))) & 1u;
                    ++total;
                }
            }
            gray[static_cast<size_t>(dy) * dw + dx] =
                total ? static_cast<uint8_t>((white * 255u + total / 2u) / total) : 255u;
        }
    }
    return WritePngRaw(path, dw, dh, 8, gray.data(), dw, dw);
}

void PrintUsage() {
    std::cerr
        << "jbig2-cli (jbig2-tools v3.0.0)\n"
        << "Usage:\n"
        << "  jbig2-cli info <in.jb2>\n"
        << "  jbig2-cli decode <in.jb2> <out.pbm|out.png> [--page N]\n"
        << "  jbig2-cli thumb <in.jb2> <out.png> <max_size>\n"
        << "  jbig2-cli encode [--symbol] [--lossy-safe] [--halftone] [--at] [--dpi N] <in1.pbm...> -o <out.jb2|out.pdf>\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { PrintUsage(); return 1; }
    const std::string cmd = argv[1];

    if (cmd == "info" && argc >= 3) {
        std::vector<uint8_t> data;
        if (!ReadFile(argv[2], data)) { std::cerr << "Cannot read: " << argv[2] << "\n"; return 2; }
        auto doc = jbig2::Document::Open(std::move(data));
        if (!doc) { std::cerr << "Invalid JBIG2 file\n"; return 3; }
        std::cout << "Pages: " << doc->PageCount() << " | Sequential: " << (doc->IsSequential() ? "yes" : "no") << "\n";
        for (uint32_t i = 0; i < doc->PageCount(); ++i) {
            auto pg = doc->DecodePage(i);
            if (pg) std::cout << "  Page " << i << ": " << pg->width << "x" << pg->height << "\n";
        }
        return 0;
    }

    if (cmd == "decode" && argc >= 4) {
        uint32_t page_idx = 0;
        for (int i = 4; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--page") page_idx = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        std::vector<uint8_t> data;
        if (!ReadFile(argv[2], data)) { std::cerr << "Cannot read: " << argv[2] << "\n"; return 2; }
        auto bmp = jbig2::DecodePage(data, page_idx);
        if (!bmp) { std::cerr << "Decode failed\n"; return 3; }
        const std::string out = argv[3];
        const bool ok = EndsWithCI(out, ".png")
            ? WritePngRaw(out, bmp->width, bmp->height, 1, bmp->pixels.data(), (bmp->width + 7u) / 8u, bmp->stride)
            : WritePbmP4(out, *bmp);
        if (!ok) { std::cerr << "Cannot write: " << out << "\n"; return 4; }
        return 0;
    }

    if (cmd == "thumb" && argc >= 5) {
        const uint32_t sz = static_cast<uint32_t>(std::stoul(argv[4]));
        std::vector<uint8_t> data;
        if (!ReadFile(argv[2], data)) { std::cerr << "Cannot read: " << argv[2] << "\n"; return 2; }
        auto bmp = jbig2::DecodePage(data, 0);
        if (!bmp) { std::cerr << "Decode failed\n"; return 3; }
        if (!WriteThumbnailPng(argv[3], *bmp, sz)) { std::cerr << "Cannot write thumbnail\n"; return 4; }
        return 0;
    }

    if (cmd == "encode") {
        jbig2::LosslessEncodeOptions opts;
        double dpi = 300.0;
        std::string out_path;
        std::vector<std::string> inputs;
        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--symbol") opts.symbol_coding = true;
            else if (a == "--lossy-safe") { opts.symbol_coding = true; opts.lossy_symbol = true; }
            else if (a == "--halftone") opts.halftone_coding = true;
            else if (a == "--at") opts.adaptive_templates = true;
            else if (a == "--dpi" && i + 1 < argc) dpi = std::stod(argv[++i]);
            else if ((a == "-o" || a == "--output") && i + 1 < argc) out_path = argv[++i];
            else inputs.push_back(a);
        }
        if (inputs.empty() || out_path.empty()) { PrintUsage(); return 1; }

        std::vector<jbig2::Bitmap1bpp> pages(inputs.size());
        std::vector<jbig2::Bitmap1bppView> views(inputs.size());
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (!ReadPbmP4(inputs[i], pages[i], dpi)) {
                std::cerr << "Cannot read P4 PBM: " << inputs[i] << "\n";
                return 2;
            }
            views[i] = {pages[i].pixels.data(), pages[i].pixels.size(),
                        pages[i].width, pages[i].height, pages[i].stride, pages[i].dpi_x, pages[i].dpi_y};
        }

        if (EndsWithCI(out_path, ".pdf")) {
            auto pdf = jbig2::EncodePdfDocumentView(views, opts);
            if (!WritePdfDocument(out_path, pages, pdf)) { std::cerr << "Write PDF failed\n"; return 4; }
        } else {
            auto jb2 = jbig2::EncodeLosslessDocumentView(views, opts);
            if (!WriteFile(out_path, jb2.data(), jb2.size())) { std::cerr << "Write JB2 failed\n"; return 4; }
        }
        return 0;
    }

    PrintUsage();
    return 1;
}
