#ifndef BITWRITER_H
#define BITWRITER_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

/**
 * BitWriter provides bit-level write operations, packing individual bits
 * into a byte buffer. Bits are written MSB-first within each byte.
 */
class BitWriter {
public:
    /** Write a single bit (0 or 1) to the buffer. */
    void writeBit(int bit) {
        if (bit != 0 && bit != 1) {
            throw std::invalid_argument("Invalid bit value: " + std::to_string(bit));
        }
        currentByte_ = (currentByte_ << 1) | bit;
        bitCount_++;
        if (bitCount_ == 8) {
            buffer_.push_back(static_cast<uint8_t>(currentByte_));
            currentByte_ = 0;
            bitCount_ = 0;
        }
    }

    /** Write a string of '0' and '1' characters as individual bits. */
    void writeBits(const std::string& bits) {
        for (char ch : bits) {
            writeBit(ch == '1' ? 1 : 0);
        }
    }

    /**
     * Flush remaining bits and return the packed buffer.
     * Any trailing bits in the last byte are left-aligned (padded with zeros
     * on the right). Returns a pair of (data, paddingBits) where paddingBits
     * is the number of zero-padding bits added to the final byte (0-7).
     */
    std::pair<std::vector<uint8_t>, int> flush() {
        int padding = 0;
        if (bitCount_ > 0) {
            padding = 8 - bitCount_;
            currentByte_ <<= padding;
            buffer_.push_back(static_cast<uint8_t>(currentByte_));
            currentByte_ = 0;
            bitCount_ = 0;
        }
        return {buffer_, padding};
    }

private:
    std::vector<uint8_t> buffer_;
    int currentByte_ = 0;
    int bitCount_ = 0;
};

/**
 * BitReader provides bit-level read operations, unpacking individual bits
 * from a byte buffer. Bits are read MSB-first within each byte.
 */
class BitReader {
public:
    /**
     * @param data        The packed byte buffer to read from.
     * @param paddingBits Number of padding bits in the last byte to ignore (0-7).
     */
    BitReader(const std::vector<uint8_t>& data, int paddingBits = 0)
        : data_(data),
          totalBits_(static_cast<int>(data.size()) * 8 - paddingBits) {}

    /** Returns true if there are more bits available to read. */
    bool hasMore() const {
        return byteIndex_ * 8 + bitIndex_ < totalBits_;
    }

    /**
     * Read a single bit (0 or 1) from the buffer.
     * Throws if no more bits are available.
     */
    int readBit() {
        if (!hasMore()) {
            throw std::runtime_error("No more bits to read");
        }
        int bit = (data_[byteIndex_] >> (7 - bitIndex_)) & 1;
        bitIndex_++;
        if (bitIndex_ == 8) {
            bitIndex_ = 0;
            byteIndex_++;
        }
        return bit;
    }

private:
    std::vector<uint8_t> data_;
    int byteIndex_ = 0;
    int bitIndex_ = 0;
    int totalBits_;
};

#endif // BITWRITER_H
