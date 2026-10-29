// Diamond Dogs Together launcher: the app icon is the project logo at every size.
//
// The icon (ddt_launcher.ico) and the in-window emblem (ddt_logo.png) are made from
// docs/readme/logo.png by tools/launcher/make_launcher_assets.py. Windows shows the
// small sizes in the taskbar, title bar, Alt-Tab and Explorer, so each one must be the
// logo, not a stand-in symbol. This test checks, independently of the generator:
//   - the icon holds exactly 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 px, 32-bit,
//     256 PNG-compressed and the others plain 32-bit bitmaps;
//   - every size matches the logo scaled down by Windows' own scaler (WIC, Fant):
//     the same silhouette, the same light/dark layout, no colour the logo lacks;
//   - the emblem matches the logo too;
//   - the built DiamondDogsTogether.exe embeds exactly these images as its only icon
//     group, and Windows loads every size from it.
//
// Usage: launcher_icon_tests <ddt_launcher.ico> <ddt_logo.png> <logo.png> <DiamondDogsTogether.exe>
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ddt_launcher_resource.h"

namespace {
unsigned checks{}, failures{};
bool Check(bool value, const std::string& label) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
    }
    return value;
}

using Bytes = std::vector<std::uint8_t>;

// Straight (not premultiplied) RGBA, top-down rows.
struct Image {
    int w{}, h{};
    Bytes rgba;
};

Bytes ReadFile(const wchar_t* path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::uint16_t U16(const Bytes& b, std::size_t at) { return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8)); }
std::uint32_t U32(const Bytes& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16)) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

template <class T>
void Release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

IWICImagingFactory* g_wic = nullptr;

// Any WIC source -> straight RGBA.
bool CopyRgba(IWICBitmapSource* source, Image& out) {
    IWICFormatConverter* converter = nullptr;
    UINT w = 0, h = 0;
    bool ok = SUCCEEDED(g_wic->CreateFormatConverter(&converter)) &&
              SUCCEEDED(converter->Initialize(source, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                              WICBitmapPaletteTypeCustom)) &&
              SUCCEEDED(converter->GetSize(&w, &h)) && w && h;
    if (ok) {
        out.w = static_cast<int>(w);
        out.h = static_cast<int>(h);
        out.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
        ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.rgba.size()), out.rgba.data()));
    }
    Release(converter);
    return ok;
}

bool DecodePng(const Bytes& png, Image& out) {
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    const bool ok = SUCCEEDED(g_wic->CreateStream(&stream)) &&
                    SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(png.data()), static_cast<DWORD>(png.size()))) &&
                    SUCCEEDED(g_wic->CreateDecoderFromStream(stream, &GUID_ContainerFormatPng, WICDecodeMetadataCacheOnLoad,
                                                             &decoder)) &&
                    SUCCEEDED(decoder->GetFrame(0, &frame)) && CopyRgba(frame, out);
    Release(frame);
    Release(decoder);
    Release(stream);
    return ok;
}

// A 32-bit icon bitmap: BITMAPINFOHEADER, BGRA rows bottom-up, then the AND mask.
bool DecodeDib(const Bytes& dib, int size, Image& out) {
    if (dib.size() < 40 || U32(dib, 0) != 40) return false;
    const auto w = static_cast<int>(U32(dib, 4));
    const auto h2 = static_cast<int>(U32(dib, 8));
    if (w != size || h2 != 2 * size || U16(dib, 12) != 1 || U16(dib, 14) != 32 || U32(dib, 16) != BI_RGB) return false;
    const std::size_t mask_stride = static_cast<std::size_t>((size + 31) / 32) * 4;
    if (dib.size() != 40 + static_cast<std::size_t>(size) * size * 4 + mask_stride * size) return false;
    out.w = out.h = size;
    out.rgba.assign(static_cast<std::size_t>(size) * size * 4, 0);
    for (int y = 0; y < size; ++y) {
        const std::size_t row = 40 + static_cast<std::size_t>(size - 1 - y) * size * 4;
        for (int x = 0; x < size; ++x) {
            const std::uint8_t* p = &dib[row + static_cast<std::size_t>(x) * 4];
            std::uint8_t* q = &out.rgba[(static_cast<std::size_t>(y) * size + x) * 4];
            q[0] = p[2];
            q[1] = p[1];
            q[2] = p[0];
            q[3] = p[3];
        }
    }
    return true;
}

// The logo scaled by WIC (Fant) to w x h, centred on a transparent square first when
// `square` (the icon is square; the logo is wider than tall), then moved down by
// `shift` logo pixels (negative: up).
bool ScaleLogo(const Image& logo, int w, int h, bool square, Image& out, int shift = 0) {
    int sw = logo.w, sh = logo.h, ox = 0, oy = 0;
    if (square) {
        sw = sh = std::max(logo.w, logo.h);
        ox = (sw - logo.w) / 2;
        oy = (sh - logo.h) / 2 + shift;
        if (oy < 0 || oy + logo.h > sh) return false;
    }
    // Premultiplied, so transparent pixels do not darken the edges.
    Bytes pbgra(static_cast<std::size_t>(sw) * sh * 4, 0);
    for (int y = 0; y < logo.h; ++y)
        for (int x = 0; x < logo.w; ++x) {
            const std::uint8_t* p = &logo.rgba[(static_cast<std::size_t>(y) * logo.w + x) * 4];
            std::uint8_t* q = &pbgra[(static_cast<std::size_t>(y + oy) * sw + x + ox) * 4];
            q[0] = static_cast<std::uint8_t>((p[2] * p[3] + 127) / 255);
            q[1] = static_cast<std::uint8_t>((p[1] * p[3] + 127) / 255);
            q[2] = static_cast<std::uint8_t>((p[0] * p[3] + 127) / 255);
            q[3] = p[3];
        }
    IWICBitmap* bitmap = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    const bool ok = SUCCEEDED(g_wic->CreateBitmapFromMemory(static_cast<UINT>(sw), static_cast<UINT>(sh), GUID_WICPixelFormat32bppPBGRA,
                                                            static_cast<UINT>(sw) * 4, static_cast<UINT>(pbgra.size()), pbgra.data(),
                                                            &bitmap)) &&
                    SUCCEEDED(g_wic->CreateBitmapScaler(&scaler)) &&
                    SUCCEEDED(scaler->Initialize(bitmap, static_cast<UINT>(w), static_cast<UINT>(h), WICBitmapInterpolationModeFant)) &&
                    CopyRgba(scaler, out);
    Release(scaler);
    Release(bitmap);
    return ok;
}

int MaxChroma(const Image& im) {
    int chroma = 0;
    for (std::size_t i = 0; i < im.rgba.size(); i += 4) {
        if (im.rgba[i + 3] < 128) continue;
        const int r = im.rgba[i], g = im.rgba[i + 1], b = im.rgba[i + 2];
        chroma = std::max(chroma, std::max({r, g, b}) - std::min({r, g, b}));
    }
    return chroma;
}

struct Likeness {
    double silhouette{};  // intersection over union of the opaque areas
    double shading{};     // correlation of the brightness over a mid-grey background
    double difference{};  // mean absolute brightness difference over that background, 0..255
};

Likeness Compare(const Image& a, const Image& b) {
    Likeness out;
    const std::size_t n = static_cast<std::size_t>(a.w) * a.h;
    std::size_t both = 0, either = 0;
    std::vector<double> la(n), lb(n);
    auto luma = [](const Bytes& p, std::size_t i) {
        const double alpha = p[i * 4 + 3] / 255.0;
        const double y = 0.299 * p[i * 4] + 0.587 * p[i * 4 + 1] + 0.114 * p[i * 4 + 2];
        return y * alpha + 128.0 * (1.0 - alpha);
    };
    double ma = 0, mb = 0, diff = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const bool oa = a.rgba[i * 4 + 3] >= 128, ob = b.rgba[i * 4 + 3] >= 128;
        both += oa && ob;
        either += oa || ob;
        la[i] = luma(a.rgba, i);
        lb[i] = luma(b.rgba, i);
        ma += la[i];
        mb += lb[i];
        diff += std::fabs(la[i] - lb[i]);
    }
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);
    double sab = 0, saa = 0, sbb = 0;
    for (std::size_t i = 0; i < n; ++i) {
        sab += (la[i] - ma) * (lb[i] - mb);
        saa += (la[i] - ma) * (la[i] - ma);
        sbb += (lb[i] - mb) * (lb[i] - mb);
    }
    out.silhouette = either ? static_cast<double>(both) / static_cast<double>(either) : 0.0;
    out.shading = (saa > 0 && sbb > 0) ? sab / std::sqrt(saa * sbb) : 0.0;
    out.difference = diff / static_cast<double>(n);
    return out;
}

struct IcoEntry {
    int size{};
    int bits{};
    int planes{};
    Bytes data;
};

std::vector<IcoEntry> ParseIco(const Bytes& ico) {
    std::vector<IcoEntry> out;
    if (!Check(ico.size() >= 6 && U16(ico, 0) == 0 && U16(ico, 2) == 1, "ddt_launcher.ico is an icon file")) return out;
    const unsigned count = U16(ico, 4);
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t at = 6 + static_cast<std::size_t>(i) * 16;
        if (!Check(at + 16 <= ico.size(), "icon directory fits the file")) break;
        IcoEntry e;
        const int w = ico[at] ? ico[at] : 256, h = ico[at + 1] ? ico[at + 1] : 256;
        Check(w == h, "icon image " + std::to_string(w) + " is square");
        e.size = w;
        e.planes = U16(ico, at + 4);
        e.bits = U16(ico, at + 6);
        const std::uint32_t bytes = U32(ico, at + 8), offset = U32(ico, at + 12);
        if (!Check(offset + static_cast<std::uint64_t>(bytes) <= ico.size(), "icon image " + std::to_string(w) + " fits the file")) break;
        e.data.assign(ico.begin() + offset, ico.begin() + offset + bytes);
        out.push_back(std::move(e));
    }
    return out;
}

bool IsPng(const Bytes& b) { return b.size() > 8 && std::memcmp(b.data(), "\x89PNG\r\n\x1a\n", 8) == 0; }

const int kSizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};

void TestIcon(const std::vector<IcoEntry>& entries, const Image& logo) {
    std::vector<int> sizes;
    for (const IcoEntry& e : entries) sizes.push_back(e.size);
    std::sort(sizes.begin(), sizes.end());
    Check(sizes == std::vector<int>(std::begin(kSizes), std::end(kSizes)),
          "the icon holds exactly 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 px");
    const int logo_chroma = MaxChroma(logo);
    for (const IcoEntry& e : entries) {
        const std::string name = "icon " + std::to_string(e.size) + " px";
        Check(e.bits == 32 && e.planes == 1, name + " is 32-bit");
        if (e.size == 256) {
            Check(IsPng(e.data), name + " is PNG-compressed");
        } else {
            Check(!IsPng(e.data), name + " is a plain bitmap");
        }
        Image icon;
        const bool decoded = IsPng(e.data) ? DecodePng(e.data, icon) : DecodeDib(e.data, e.size, icon);
        if (!Check(decoded && icon.w == e.size && icon.h == e.size, name + " decodes at its size")) continue;
        // The generator may move the logo up or down by under half a pixel (to put the
        // diamond's top edge on a pixel boundary): compare at the best such offset.
        Likeness like;
        bool scaled = false;
        const double logo_px = static_cast<double>(std::max(logo.w, logo.h)) / e.size;  // logo pixels per icon pixel
        for (int step = -4; step <= 4; ++step) {
            Image reference;
            if (!ScaleLogo(logo, e.size, e.size, true, reference, static_cast<int>(std::lround(step / 8.0 * logo_px)))) continue;
            scaled = true;
            const Likeness at = Compare(icon, reference);
            if (at.shading > like.shading) like = at;
        }
        if (!Check(scaled, name + ": the logo scales")) continue;
        std::printf("%-12s silhouette %.3f  shading %.3f  difference %5.1f  chroma %d (logo %d)\n", name.c_str(), like.silhouette,
                    like.shading, like.difference, MaxChroma(icon), logo_chroma);
        // A stand-in symbol (the old shield with an orange diamond) scores about 0.8 / -0.15 / 55.
        Check(like.silhouette >= 0.95, name + " has the logo's silhouette");
        Check(like.shading >= 0.85, name + " has the logo's light and dark layout");
        Check(like.difference <= 20.0, name + " is close to the scaled logo");
        Check(MaxChroma(icon) <= logo_chroma + 16, name + " uses no colour the logo lacks");
    }
}

void TestEmblem(const Bytes& png, const Image& logo) {
    Image emblem, reference;
    if (!Check(DecodePng(png, emblem), "ddt_logo.png decodes")) return;
    const int expected_h = static_cast<int>(std::lround(256.0 * logo.h / logo.w));
    Check(emblem.w == 256 && emblem.h == expected_h, "the emblem is the logo at 256 px wide");
    if (!Check(ScaleLogo(logo, emblem.w, emblem.h, false, reference), "the logo scales to the emblem size")) return;
    const Likeness like = Compare(emblem, reference);
    std::printf("emblem       silhouette %.3f  shading %.3f  difference %5.1f\n", like.silhouette, like.shading, like.difference);
    Check(like.silhouette >= 0.97 && like.shading >= 0.97 && like.difference <= 6.0, "the emblem matches the logo");
}

// The exe's icon group must be exactly the .ico's images, and the only group (so
// Explorer, the taskbar and the window all use it).
void TestExe(const wchar_t* exe, const std::vector<IcoEntry>& entries) {
    HMODULE module = LoadLibraryExW(exe, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!Check(module != nullptr, "DiamondDogsTogether.exe opens as a resource file")) return;
    std::vector<ULONG_PTR> groups;
    EnumResourceNamesW(
        module, RT_GROUP_ICON,
        [](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param) -> BOOL {
            reinterpret_cast<std::vector<ULONG_PTR>*>(param)->push_back(IS_INTRESOURCE(name) ? reinterpret_cast<ULONG_PTR>(name) : 0);
            return TRUE;
        },
        reinterpret_cast<LONG_PTR>(&groups));
    Check(groups.size() == 1 && groups[0] == IDI_DDT_APP, "the exe has one icon group, IDI_DDT_APP");
    HRSRC group_res = FindResourceW(module, MAKEINTRESOURCEW(IDI_DDT_APP), RT_GROUP_ICON);
    HGLOBAL group_mem = group_res ? LoadResource(module, group_res) : nullptr;
    const auto* group = static_cast<const std::uint8_t*>(group_mem ? LockResource(group_mem) : nullptr);
    if (Check(group != nullptr, "the exe's icon group loads")) {
        const Bytes dir(group, group + SizeofResource(module, group_res));
        const unsigned count = dir.size() >= 6 ? U16(dir, 4) : 0;
        Check(count == entries.size() && dir.size() == 6 + count * 14u, "the exe's icon group lists every icon size");
        unsigned matched = 0;
        for (unsigned i = 0; i < count && 6 + (i + 1) * 14u <= dir.size(); ++i) {
            const std::size_t at = 6 + static_cast<std::size_t>(i) * 14;
            const int size = dir[at] ? dir[at] : 256;
            const WORD id = U16(dir, at + 12);
            HRSRC icon_res = FindResourceW(module, MAKEINTRESOURCEW(id), RT_ICON);
            HGLOBAL icon_mem = icon_res ? LoadResource(module, icon_res) : nullptr;
            const auto* bytes = static_cast<const std::uint8_t*>(icon_mem ? LockResource(icon_mem) : nullptr);
            if (!bytes) continue;
            const Bytes data(bytes, bytes + SizeofResource(module, icon_res));
            for (const IcoEntry& e : entries) matched += e.size == size && e.data == data;
        }
        Check(matched == entries.size(), "the exe embeds every image of ddt_launcher.ico unchanged");
    }
    FreeLibrary(module);

    // Windows itself loads each size from the exe at that exact size.
    module = LoadLibraryExW(exe, nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!module) return;
    for (int size : kSizes) {
        HICON icon = static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(IDI_DDT_APP), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
        ICONINFO info{};
        BITMAP bm{};
        const bool ok = icon && GetIconInfo(icon, &info) && info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm) &&
                        bm.bmWidth == size && bm.bmHeight == size;
        Check(ok, "Windows loads the " + std::to_string(size) + " px icon from the exe");
        if (info.hbmColor) DeleteObject(info.hbmColor);
        if (info.hbmMask) DeleteObject(info.hbmMask);
        if (icon) DestroyIcon(icon);
    }
    FreeLibrary(module);
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: launcher_icon_tests <ddt_launcher.ico> <ddt_logo.png> <logo.png> <DiamondDogsTogether.exe>\n");
        return 2;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic)))) {
        std::fprintf(stderr, "FAIL: WIC is unavailable\n");
        return 1;
    }
    Image logo;
    if (Check(DecodePng(ReadFile(argv[3]), logo), "docs/readme/logo.png decodes")) {
        const Bytes ico = ReadFile(argv[1]);
        const std::vector<IcoEntry> entries = ParseIco(ico);
        TestIcon(entries, logo);
        TestEmblem(ReadFile(argv[2]), logo);
        TestExe(argv[4], entries);
    }
    Release(g_wic);
    CoUninitialize();
    std::printf("launcher_icon_tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
