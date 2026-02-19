#ifndef HUFFMAN_H
#define HUFFMAN_H

#include "bitwriter.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

/**
 * A node in the Huffman tree. Leaf nodes hold a symbol (byte value);
 * internal nodes have left and right children.
 */
struct HuffmanNode {
    int symbol;  // -1 means internal node (no symbol)
    int frequency;
    std::shared_ptr<HuffmanNode> left;
    std::shared_ptr<HuffmanNode> right;

    HuffmanNode(int sym, int freq,
                std::shared_ptr<HuffmanNode> l = nullptr,
                std::shared_ptr<HuffmanNode> r = nullptr)
        : symbol(sym), frequency(freq), left(std::move(l)), right(std::move(r)) {}

    bool isLeaf() const { return left == nullptr && right == nullptr; }
};

/**
 * Serialized Huffman-encoded data, containing everything needed to decode.
 */
struct HuffmanEncoded {
    std::vector<uint8_t> data;           // The encoded bit data packed into bytes
    int paddingBits;                      // Number of padding bits in the last byte
    std::map<int, std::string> codes;     // Code table: byte value -> bit string
    size_t originalLength;                // Original data length in bytes
};

/** Build a frequency table mapping each byte value to its occurrence count. */
inline std::map<int, int> buildFrequencyTable(const std::vector<uint8_t>& data) {
    std::map<int, int> freq;
    for (uint8_t byte : data) {
        freq[byte]++;
    }
    return freq;
}

/**
 * Build a Huffman tree from a frequency table.
 * Returns the root node of the tree, or nullptr if the table is empty.
 */
inline std::shared_ptr<HuffmanNode> buildHuffmanTree(const std::map<int, int>& frequencies) {
    if (frequencies.empty()) {
        return nullptr;
    }

    // Create leaf nodes, sorted descending by frequency so we can pop from the back.
    std::vector<std::shared_ptr<HuffmanNode>> nodes;
    for (const auto& [symbol, freq] : frequencies) {
        nodes.push_back(std::make_shared<HuffmanNode>(symbol, freq));
    }
    std::sort(nodes.begin(), nodes.end(),
              [](const auto& a, const auto& b) { return a->frequency > b->frequency; });

    // Special case: single unique symbol.
    if (nodes.size() == 1) {
        auto leaf = nodes[0];
        return std::make_shared<HuffmanNode>(-1, leaf->frequency, leaf);
    }

    while (nodes.size() > 1) {
        auto right = nodes.back(); nodes.pop_back();
        auto left  = nodes.back(); nodes.pop_back();
        auto parent = std::make_shared<HuffmanNode>(
            -1, left->frequency + right->frequency, left, right);

        // Binary-search insert to keep the list sorted descending.
        int parentFreq = parent->frequency;
        auto it = std::lower_bound(nodes.begin(), nodes.end(), parentFreq,
            [](const std::shared_ptr<HuffmanNode>& node, int freq) {
                return node->frequency > freq;
            });
        nodes.insert(it, parent);
    }

    return nodes[0];
}

/**
 * Generate Huffman codes by traversing the tree.
 * Returns a map from byte value to its binary code string (e.g. "0110").
 */
inline std::map<int, std::string> generateCodes(const std::shared_ptr<HuffmanNode>& root) {
    std::map<int, std::string> codes;
    if (!root) return codes;

    // Recursive traversal helper.
    struct Traverser {
        std::map<int, std::string>& codes;
        void traverse(const std::shared_ptr<HuffmanNode>& node, const std::string& code) {
            if (node->isLeaf() && node->symbol != -1) {
                codes[node->symbol] = code.empty() ? "0" : code;
                return;
            }
            if (node->left)  traverse(node->left,  code + "0");
            if (node->right) traverse(node->right, code + "1");
        }
    };

    Traverser t{codes};
    t.traverse(root, "");
    return codes;
}

/**
 * Encode a byte vector using Huffman coding.
 * Returns a HuffmanEncoded object containing the compressed data and metadata.
 */
inline HuffmanEncoded encode(const std::vector<uint8_t>& input) {
    if (input.empty()) {
        return {{}, 0, {}, 0};
    }

    auto frequencies = buildFrequencyTable(input);
    auto tree = buildHuffmanTree(frequencies);
    auto codes = generateCodes(tree);

    BitWriter writer;
    for (uint8_t byte : input) {
        auto it = codes.find(byte);
        if (it == codes.end()) {
            throw std::runtime_error("No Huffman code for byte " + std::to_string(byte));
        }
        writer.writeBits(it->second);
    }

    auto [data, paddingBits] = writer.flush();
    return {data, paddingBits, codes, input.size()};
}

/**
 * Decode Huffman-encoded data back to the original byte vector.
 */
inline std::vector<uint8_t> decode(const HuffmanEncoded& encoded) {
    if (encoded.originalLength == 0) {
        return {};
    }

    // Build a reverse lookup tree from codes for efficient decoding.
    auto root = std::make_shared<HuffmanNode>(-1, 0);
    for (const auto& [symbol, code] : encoded.codes) {
        auto node = root;
        for (char bit : code) {
            if (bit == '0') {
                if (!node->left) node->left = std::make_shared<HuffmanNode>(-1, 0);
                node = node->left;
            } else {
                if (!node->right) node->right = std::make_shared<HuffmanNode>(-1, 0);
                node = node->right;
            }
        }
        node->symbol = symbol;
    }

    BitReader reader(encoded.data, encoded.paddingBits);
    std::vector<uint8_t> output;
    output.reserve(encoded.originalLength);

    while (output.size() < encoded.originalLength) {
        auto node = root;
        while (!node->isLeaf()) {
            int bit = reader.readBit();
            if (bit == 0) {
                if (!node->left) throw std::runtime_error("Invalid data: missing left child");
                node = node->left;
            } else {
                if (!node->right) throw std::runtime_error("Invalid data: missing right child");
                node = node->right;
            }
        }
        if (node->symbol == -1) {
            throw std::runtime_error("Invalid data: leaf node has no symbol");
        }
        output.push_back(static_cast<uint8_t>(node->symbol));
    }

    return output;
}

// ---------------------------------------------------------------------------
// File I/O helpers: save encoded data to a .huf file and load it back.
// ---------------------------------------------------------------------------

/**
 * Write a HuffmanEncoded structure to a binary file.
 *
 * File format (all integers little-endian):
 *   [4 bytes] magic "HUF\0"
 *   [4 bytes] originalLength
 *   [4 bytes] paddingBits
 *   [4 bytes] number of codes (N)
 *   For each code entry:
 *       [1 byte]  symbol
 *       [2 bytes] code length
 *       [variable] code string bytes (no null terminator)
 *   [4 bytes] encoded data length (in bytes)
 *   [variable] encoded data bytes
 */
inline void saveEncoded(const std::string& path, const HuffmanEncoded& enc) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open file for writing: " + path);

    auto writeU32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

    // Magic
    out.write("HUF", 4);  // includes '\0'

    writeU32(static_cast<uint32_t>(enc.originalLength));
    writeU32(static_cast<uint32_t>(enc.paddingBits));
    writeU32(static_cast<uint32_t>(enc.codes.size()));

    for (const auto& [symbol, code] : enc.codes) {
        uint8_t sym = static_cast<uint8_t>(symbol);
        out.write(reinterpret_cast<const char*>(&sym), 1);
        writeU16(static_cast<uint16_t>(code.size()));
        out.write(code.data(), static_cast<std::streamsize>(code.size()));
    }

    writeU32(static_cast<uint32_t>(enc.data.size()));
    if (!enc.data.empty()) {
        out.write(reinterpret_cast<const char*>(enc.data.data()),
                  static_cast<std::streamsize>(enc.data.size()));
    }
}

/** Read a HuffmanEncoded structure from a binary .huf file. */
inline HuffmanEncoded loadEncoded(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file for reading: " + path);

    auto readU32 = [&]() -> uint32_t {
        uint32_t v = 0;
        in.read(reinterpret_cast<char*>(&v), 4);
        return v;
    };
    auto readU16 = [&]() -> uint16_t {
        uint16_t v = 0;
        in.read(reinterpret_cast<char*>(&v), 2);
        return v;
    };

    char magic[4]{};
    in.read(magic, 4);
    if (std::string(magic, 3) != "HUF") {
        throw std::runtime_error("Invalid file format (bad magic)");
    }

    HuffmanEncoded enc{};
    enc.originalLength = readU32();
    enc.paddingBits    = static_cast<int>(readU32());
    uint32_t numCodes  = readU32();

    for (uint32_t i = 0; i < numCodes; i++) {
        uint8_t sym = 0;
        in.read(reinterpret_cast<char*>(&sym), 1);
        uint16_t codeLen = readU16();
        std::string code(codeLen, '\0');
        in.read(code.data(), codeLen);
        enc.codes[sym] = code;
    }

    uint32_t dataLen = readU32();
    enc.data.resize(dataLen);
    if (dataLen > 0) {
        in.read(reinterpret_cast<char*>(enc.data.data()), dataLen);
    }

    return enc;
}

#endif // HUFFMAN_H
