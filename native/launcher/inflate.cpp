// Project-authored DEFLATE decoder, written from RFC 1951 and RFC 1950.
//
// Codes are canonical Huffman codes rebuilt from their code lengths. Short
// codes (up to kFastBits) are resolved with one table lookup on the next
// input bits; longer codes walk the canonical code ranges length by length.
#include "launcher/inflate.hpp"

#include <algorithm>
#include <array>

#include "launcher/crc32.hpp"

namespace ddt {
namespace {

constexpr int kMaxBits = 15;
constexpr int kFastBits = 10;
constexpr int kMaxSymbols = 288;
constexpr int kTruncated = -1;
constexpr int kInvalid = -2;

// RFC 1951 section 3.2.5.
constexpr std::uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                           31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistanceBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                             193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                             6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
// RFC 1951 section 3.2.7: the order code-length code lengths are sent in.
constexpr std::uint8_t kCodeLengthOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

struct Huffman {
    std::uint16_t count[kMaxBits + 1];   // number of codes of each length
    std::uint16_t symbols[kMaxSymbols];  // symbols ordered by (code length, symbol value)
    std::uint16_t fast[1u << kFastBits]; // (length << 12) | symbol, or 0 when the code is longer
};

enum class Shape { complete, incomplete, oversubscribed };

std::uint32_t ReverseBits(std::uint32_t value, int bits) {
    std::uint32_t out = 0;
    for (int i = 0; i < bits; ++i) {
        out = (out << 1) | (value & 1u);
        value >>= 1;
    }
    return out;
}

// `lengths` holds one code length (0..15) per symbol; 0 means unused.
Shape Build(Huffman& h, const std::uint8_t* lengths, int symbol_count) {
    std::fill(std::begin(h.count), std::end(h.count), std::uint16_t{0});
    std::fill(std::begin(h.fast), std::end(h.fast), std::uint16_t{0});
    for (int s = 0; s < symbol_count; ++s) ++h.count[lengths[s]];
    int left = 1;  // code space still unused at the current length
    for (int len = 1; len <= kMaxBits; ++len) {
        left = (left << 1) - h.count[len];
        if (left < 0) return Shape::oversubscribed;
    }
    std::uint16_t next[kMaxBits + 2] = {};
    for (int len = 1; len <= kMaxBits; ++len) next[len + 1] = static_cast<std::uint16_t>(next[len] + h.count[len]);
    for (int s = 0; s < symbol_count; ++s)
        if (lengths[s]) h.symbols[next[lengths[s]]++] = static_cast<std::uint16_t>(s);
    // Canonical codes: within one length, codes are consecutive in symbol order;
    // the first code of each length follows on from the previous length.
    std::uint32_t code = 0;
    int index = 0;
    for (int len = 1; len <= kFastBits; ++len) {
        for (int k = 0; k < h.count[len]; ++k) {
            const std::uint16_t entry = static_cast<std::uint16_t>(len << 12 | h.symbols[index + k]);
            for (std::uint32_t slot = ReverseBits(code + static_cast<std::uint32_t>(k), len); slot < (1u << kFastBits);
                 slot += 1u << len)
                h.fast[slot] = entry;
        }
        index += h.count[len];
        code = (code + h.count[len]) << 1;
    }
    return left > 0 ? Shape::incomplete : Shape::complete;
}

class BitReader {
public:
    BitReader(const std::uint8_t* src, std::size_t size) : src_(src), size_(size) {}

    void Refill() {
        while (count_ <= 56 && pos_ < size_) {
            bits_ |= static_cast<std::uint64_t>(src_[pos_++]) << count_;
            count_ += 8;
        }
    }
    bool Take(int n, std::uint32_t& value) {
        if (count_ < n) Refill();
        if (count_ < n) return false;
        value = static_cast<std::uint32_t>(bits_ & ((1ull << n) - 1));
        Drop(n);
        return true;
    }
    void Drop(int n) {
        bits_ >>= n;
        count_ -= n;
    }
    std::uint64_t bits() const { return bits_; }
    int count() const { return count_; }
    // Discards the rest of the current byte and hands whole buffered bytes back
    // to the input, so stored-block bytes can be copied directly.
    void AlignAndUnread() {
        Drop(count_ & 7);
        pos_ -= static_cast<std::size_t>(count_ / 8);
        bits_ = 0;
        count_ = 0;
    }
    std::size_t position() const { return pos_; }
    void Skip(std::size_t n) { pos_ += n; }
    std::size_t Consumed() const { return pos_ - static_cast<std::size_t>(count_ / 8); }
    const std::uint8_t* src() const { return src_; }
    std::size_t size() const { return size_; }

private:
    const std::uint8_t* src_;
    std::size_t size_;
    std::size_t pos_ = 0;
    std::uint64_t bits_ = 0;
    int count_ = 0;
};

int Decode(BitReader& in, const Huffman& h) {
    in.Refill();
    const std::uint16_t entry = h.fast[in.bits() & ((1u << kFastBits) - 1)];
    if (entry) {
        const int len = entry >> 12;
        if (len > in.count()) return kTruncated;
        in.Drop(len);
        return entry & 0xFFF;
    }
    const std::uint64_t bits = in.bits();
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code |= static_cast<int>((bits >> (len - 1)) & 1u);
        const int n = h.count[len];
        if (code >= first && code - first < n) {
            if (len > in.count()) return kTruncated;
            in.Drop(len);
            return h.symbols[index + code - first];
        }
        index += n;
        first = (first + n) << 1;
        code <<= 1;
    }
    return in.count() < kMaxBits ? kTruncated : kInvalid;
}

struct FixedCodes {
    Huffman literal;
    Huffman distance;
    FixedCodes() {
        std::uint8_t lengths[kMaxSymbols];
        for (int s = 0; s < 144; ++s) lengths[s] = 8;
        for (int s = 144; s < 256; ++s) lengths[s] = 9;
        for (int s = 256; s < 280; ++s) lengths[s] = 7;
        for (int s = 280; s < 288; ++s) lengths[s] = 8;
        Build(literal, lengths, 288);
        for (int s = 0; s < 30; ++s) lengths[s] = 5;
        Build(distance, lengths, 30);
    }
};

const FixedCodes& Fixed() {
    static const FixedCodes codes;
    return codes;
}

class Inflater {
public:
    Inflater(const std::uint8_t* src, std::size_t size, std::vector<std::uint8_t>& out, std::size_t max_output)
        : in_(src, size), out_(out), base_(out.size()), max_output_(max_output) {}

    bool Run(std::string& error) {
        bool last = false;
        do {
            std::uint32_t header = 0;
            if (!in_.Take(3, header)) return Fail(error, "truncated block header");
            last = (header & 1u) != 0;
            switch (header >> 1) {
                case 0:
                    if (!Stored(error)) return false;
                    break;
                case 1:
                    if (!Codes(Fixed().literal, Fixed().distance, error)) return false;
                    break;
                case 2:
                    if (!Dynamic(error)) return false;
                    break;
                default:
                    return Fail(error, "invalid block type 3");
            }
        } while (!last);
        return true;
    }
    std::size_t Consumed() const { return in_.Consumed(); }

private:
    static bool Fail(std::string& error, const char* what) {
        error = std::string("inflate: ") + what;
        return false;
    }
    bool Room(std::size_t n, std::string& error) {
        if (out_.size() - base_ > max_output_ || n > max_output_ - (out_.size() - base_))
            return Fail(error, "output is larger than allowed");
        return true;
    }

    bool Stored(std::string& error) {
        in_.AlignAndUnread();
        const std::size_t pos = in_.position();
        if (in_.size() - pos < 4) return Fail(error, "truncated stored block header");
        const std::uint8_t* p = in_.src() + pos;
        const std::uint32_t len = p[0] | p[1] << 8u;
        const std::uint32_t nlen = p[2] | p[3] << 8u;
        if (len != (~nlen & 0xFFFFu)) return Fail(error, "stored block length check failed");
        if (in_.size() - pos - 4 < len) return Fail(error, "truncated stored block");
        if (!Room(len, error)) return false;
        out_.insert(out_.end(), p + 4, p + 4 + len);
        in_.Skip(4 + static_cast<std::size_t>(len));
        return true;
    }

    bool Dynamic(std::string& error) {
        std::uint32_t hlit = 0, hdist = 0, hclen = 0;
        if (!in_.Take(5, hlit) || !in_.Take(5, hdist) || !in_.Take(4, hclen))
            return Fail(error, "truncated dynamic block header");
        hlit += 257;
        hdist += 1;
        hclen += 4;
        if (hlit > 286 || hdist > 30) return Fail(error, "too many length or distance codes");
        std::uint8_t lengths[320] = {};
        for (std::uint32_t i = 0; i < hclen; ++i) {
            std::uint32_t value = 0;
            if (!in_.Take(3, value)) return Fail(error, "truncated code length codes");
            lengths[kCodeLengthOrder[i]] = static_cast<std::uint8_t>(value);
        }
        Huffman code_lengths;
        if (Build(code_lengths, lengths, 19) != Shape::complete) return Fail(error, "invalid code length code");
        std::fill(std::begin(lengths), std::end(lengths), std::uint8_t{0});
        const std::uint32_t total = hlit + hdist;
        std::uint32_t index = 0;
        while (index < total) {
            const int symbol = Decode(in_, code_lengths);
            if (symbol < 0) return Fail(error, symbol == kTruncated ? "truncated code lengths" : "invalid code length symbol");
            if (symbol < 16) {
                lengths[index++] = static_cast<std::uint8_t>(symbol);
                continue;
            }
            std::uint8_t value = 0;
            std::uint32_t repeat = 0, extra = 0;
            if (symbol == 16) {
                if (index == 0) return Fail(error, "repeat with no previous length");
                value = lengths[index - 1];
                if (!in_.Take(2, extra)) return Fail(error, "truncated code lengths");
                repeat = 3 + extra;
            } else if (symbol == 17) {
                if (!in_.Take(3, extra)) return Fail(error, "truncated code lengths");
                repeat = 3 + extra;
            } else {
                if (!in_.Take(7, extra)) return Fail(error, "truncated code lengths");
                repeat = 11 + extra;
            }
            if (repeat > total - index) return Fail(error, "code lengths overflow");
            while (repeat--) lengths[index++] = value;
        }
        if (lengths[256] == 0) return Fail(error, "missing end-of-block code");
        Huffman literal, distance;
        // An incomplete code is only acceptable when it holds a single one-bit code.
        Shape shape = Build(literal, lengths, static_cast<int>(hlit));
        if (shape == Shape::oversubscribed ||
            (shape == Shape::incomplete && literal.count[0] + literal.count[1] != static_cast<int>(hlit)))
            return Fail(error, "invalid literal/length code");
        shape = Build(distance, lengths + hlit, static_cast<int>(hdist));
        if (shape == Shape::oversubscribed ||
            (shape == Shape::incomplete && distance.count[0] + distance.count[1] != static_cast<int>(hdist)))
            return Fail(error, "invalid distance code");
        return Codes(literal, distance, error);
    }

    bool Codes(const Huffman& literal, const Huffman& distance, std::string& error) {
        for (;;) {
            int symbol = Decode(in_, literal);
            if (symbol < 0) return Fail(error, symbol == kTruncated ? "truncated data" : "invalid literal/length code");
            if (symbol < 256) {
                if (!Room(1, error)) return false;
                out_.push_back(static_cast<std::uint8_t>(symbol));
                continue;
            }
            if (symbol == 256) return true;
            symbol -= 257;
            if (symbol >= 29) return Fail(error, "invalid length symbol");
            std::uint32_t extra = 0;
            if (!in_.Take(kLengthExtra[symbol], extra)) return Fail(error, "truncated data");
            const std::size_t length = kLengthBase[symbol] + extra;
            symbol = Decode(in_, distance);
            if (symbol < 0) return Fail(error, symbol == kTruncated ? "truncated data" : "invalid distance code");
            if (symbol >= 30) return Fail(error, "invalid distance symbol");
            if (!in_.Take(kDistanceExtra[symbol], extra)) return Fail(error, "truncated data");
            const std::size_t dist = kDistanceBase[symbol] + extra;
            const std::size_t produced = out_.size() - base_;
            if (dist > produced) return Fail(error, "distance is too far back");
            if (!Room(length, error)) return false;
            const std::size_t to = out_.size();
            out_.resize(to + length);
            std::uint8_t* data = out_.data();
            for (std::size_t i = 0; i < length; ++i) data[to + i] = data[to - dist + i];
        }
    }

    BitReader in_;
    std::vector<std::uint8_t>& out_;
    std::size_t base_;
    std::size_t max_output_;
};

}  // namespace

bool InflateRaw(const std::uint8_t* src, std::size_t size, std::vector<std::uint8_t>& out, std::size_t max_output,
                std::string& error, std::size_t* consumed) {
    Inflater inflater(src, size, out, max_output);
    if (!inflater.Run(error)) return false;
    if (consumed) *consumed = inflater.Consumed();
    return true;
}

bool ZlibDecompress(const std::uint8_t* src, std::size_t size, std::vector<std::uint8_t>& out, std::size_t max_output,
                    std::string& error) {
    if (size < 2) {
        error = "zlib: truncated header";
        return false;
    }
    const unsigned cmf = src[0], flg = src[1];
    if ((cmf & 0x0Fu) != 8) {
        error = "zlib: compression method is not deflate";
        return false;
    }
    if ((cmf >> 4) > 7) {
        error = "zlib: window size is too large";
        return false;
    }
    if ((cmf << 8 | flg) % 31 != 0) {
        error = "zlib: header check failed";
        return false;
    }
    if (flg & 0x20u) {
        error = "zlib: preset dictionaries are not supported";
        return false;
    }
    const std::size_t base = out.size();
    std::size_t used = 0;
    if (!InflateRaw(src + 2, size - 2, out, max_output, error, &used)) return false;
    if (size - 2 - used < 4) {
        error = "zlib: missing Adler-32 trailer";
        return false;
    }
    const std::uint8_t* t = src + 2 + used;
    const std::uint32_t expected = static_cast<std::uint32_t>(t[0]) << 24 | static_cast<std::uint32_t>(t[1]) << 16 |
                                   static_cast<std::uint32_t>(t[2]) << 8 | t[3];
    if (Adler32(out.data() + base, out.size() - base) != expected) {
        error = "zlib: Adler-32 mismatch";
        return false;
    }
    return true;
}

}  // namespace ddt
