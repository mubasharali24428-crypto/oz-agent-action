/**
 * huffman_compressor.cpp
 *
 * A single, self-contained Huffman file compressor/decompressor.
 * Open this file in VS Code and compile & run it directly.
 *
 * Build (terminal):
 *   g++ -std=c++17 -O2 -o huffman_compressor huffman_compressor.cpp
 *   ./huffman_compressor
 *
 * Or on Windows with MSVC:
 *   cl /std:c++17 /EHsc /O2 huffman_compressor.cpp
 *   huffman_compressor.exe
 *
 * Features:
 *   - Compress any file using Huffman coding
 *   - Decompress .huf files back to originals
 *   - Browse and select files from Desktop, current directory, or custom path
 */

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// ===========================================================================
// BitWriter – packs individual bits into a byte buffer (MSB-first)
// ===========================================================================

class BitWriter {
public:
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

    void writeBits(const std::string& bits) {
        for (char ch : bits) {
            if (ch != '0' && ch != '1') {
                throw std::invalid_argument(std::string("Invalid bit character: ") + ch);
            }
            writeBit(ch == '1' ? 1 : 0);
        }
    }

    /** Flush and return (packedBytes, paddingBits). */
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

// ===========================================================================
// BitReader – unpacks individual bits from a byte buffer (MSB-first)
// ===========================================================================

class BitReader {
public:
    BitReader(const std::vector<uint8_t>& data, int paddingBits = 0)
        : data_(data),
          totalBits_(static_cast<int>(data.size()) * 8 - paddingBits) {}

    bool hasMore() const {
        return byteIndex_ * 8 + bitIndex_ < totalBits_;
    }

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

// ===========================================================================
// HuffmanNode – tree node (leaf = symbol, internal = children)
// ===========================================================================

struct HuffmanNode {
    int symbol;  // -1 for internal nodes
    int frequency;
    std::shared_ptr<HuffmanNode> left;
    std::shared_ptr<HuffmanNode> right;

    HuffmanNode(int sym, int freq,
                std::shared_ptr<HuffmanNode> l = nullptr,
                std::shared_ptr<HuffmanNode> r = nullptr)
        : symbol(sym), frequency(freq), left(std::move(l)), right(std::move(r)) {}

    bool isLeaf() const { return left == nullptr && right == nullptr; }
};

// ===========================================================================
// HuffmanEncoded – everything needed to reconstruct the original data
// ===========================================================================

struct HuffmanEncoded {
    std::vector<uint8_t> data;
    int paddingBits;
    std::map<int, std::string> codes;
    size_t originalLength;
};

// ===========================================================================
// Huffman algorithm helpers
// ===========================================================================

static std::map<int, int> buildFrequencyTable(const std::vector<uint8_t>& data) {
    std::map<int, int> freq;
    for (uint8_t byte : data) {
        freq[byte]++;
    }
    return freq;
}

static std::shared_ptr<HuffmanNode> buildHuffmanTree(const std::map<int, int>& frequencies) {
    if (frequencies.empty()) return nullptr;

    std::vector<std::shared_ptr<HuffmanNode>> nodes;
    for (const auto& [symbol, freq] : frequencies) {
        nodes.push_back(std::make_shared<HuffmanNode>(symbol, freq));
    }
    std::sort(nodes.begin(), nodes.end(),
              [](const auto& a, const auto& b) { return a->frequency > b->frequency; });

    if (nodes.size() == 1) {
        return std::make_shared<HuffmanNode>(-1, nodes[0]->frequency, nodes[0]);
    }

    while (nodes.size() > 1) {
        auto right = nodes.back(); nodes.pop_back();
        auto left  = nodes.back(); nodes.pop_back();
        auto parent = std::make_shared<HuffmanNode>(
            -1, left->frequency + right->frequency, left, right);

        int parentFreq = parent->frequency;
        auto it = std::lower_bound(nodes.begin(), nodes.end(), parentFreq,
            [](const std::shared_ptr<HuffmanNode>& node, int freq) {
                return node->frequency > freq;
            });
        nodes.insert(it, parent);
    }
    return nodes[0];
}

static std::map<int, std::string> generateCodes(const std::shared_ptr<HuffmanNode>& root) {
    std::map<int, std::string> codes;
    if (!root) return codes;

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

// ===========================================================================
// Encode / Decode
// ===========================================================================

static HuffmanEncoded encode(const std::vector<uint8_t>& input) {
    if (input.empty()) return {{}, 0, {}, 0};

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

static std::vector<uint8_t> decode(const HuffmanEncoded& encoded) {
    if (encoded.originalLength == 0) return {};

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

// ===========================================================================
// .huf file I/O  (binary format)
// ===========================================================================

static void saveEncoded(const std::string& path, const HuffmanEncoded& enc) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open file for writing: " + path);

    auto writeU32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

    out.write("HUF", 4);  // magic (includes '\0')
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

static HuffmanEncoded loadEncoded(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file for reading: " + path);

    auto readU32 = [&]() -> uint32_t {
        uint32_t v = 0; in.read(reinterpret_cast<char*>(&v), 4); return v;
    };
    auto readU16 = [&]() -> uint16_t {
        uint16_t v = 0; in.read(reinterpret_cast<char*>(&v), 2); return v;
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

// ===========================================================================
// File helpers
// ===========================================================================

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open file: " + path);
    auto size = in.tellg();
    in.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

static void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open file for writing: " + path);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

// ===========================================================================
// Interactive file browser
// ===========================================================================

static fs::path getDesktopPath() {
    if (const char* desktop = std::getenv("XDG_DESKTOP_DIR")) return fs::path(desktop);
    if (const char* home = std::getenv("HOME"))               return fs::path(home) / "Desktop";
    if (const char* userprofile = std::getenv("USERPROFILE"))  return fs::path(userprofile) / "Desktop";
    return fs::current_path();
}

static std::string browseAndSelectFile(const fs::path& directory) {
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        std::cerr << "Directory does not exist: " << directory << "\n";
        return {};
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }

    if (files.empty()) {
        std::cout << "No files found in " << directory << "\n";
        return {};
    }

    std::sort(files.begin(), files.end());

    std::cout << "\n--- Files in " << directory << " ---\n";
    for (size_t i = 0; i < files.size(); i++) {
        std::cout << "  [" << i + 1 << "] " << files[i].filename().string() << "\n";
    }
    std::cout << "  [0] Cancel\n";
    std::cout << "Select a file: ";

    int choice = -1;
    if (!(std::cin >> choice) || choice < 0 || choice > static_cast<int>(files.size())) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "Invalid selection.\n";
        return {};
    }
    std::cin.ignore(10000, '\n');

    if (choice == 0) return {};
    return files[static_cast<size_t>(choice - 1)].string();
}

// ===========================================================================
// Menu actions
// ===========================================================================

static void compressFile() {
    std::cout << "\n=== Compress a File ===\n";
    std::cout << "Browse files from:\n";
    std::cout << "  [1] Desktop\n";
    std::cout << "  [2] Current directory\n";
    std::cout << "  [3] Enter a custom path\n";
    std::cout << "Choice: ";

    int locChoice = 0;
    if (!(std::cin >> locChoice)) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "Invalid choice.\n";
        return;
    }
    std::cin.ignore(10000, '\n');

    std::string inputPath;
    switch (locChoice) {
        case 1: inputPath = browseAndSelectFile(getDesktopPath());    break;
        case 2: inputPath = browseAndSelectFile(fs::current_path());  break;
        case 3: {
            std::cout << "Enter directory path to browse: ";
            std::string dir;
            std::getline(std::cin, dir);
            inputPath = browseAndSelectFile(fs::path(dir));
            break;
        }
        default:
            std::cout << "Invalid choice.\n";
            return;
    }

    if (inputPath.empty()) { std::cout << "No file selected.\n"; return; }

    std::cout << "Reading " << inputPath << " ...\n";
    auto data = readFile(inputPath);
    std::cout << "Original size: " << data.size() << " bytes\n";

    std::cout << "Compressing ...\n";
    auto encoded = encode(data);
    std::cout << "Compressed to " << encoded.data.size() << " bytes ("
              << encoded.codes.size() << " unique symbols)\n";

    std::string outPath = inputPath + ".huf";
    std::cout << "Save compressed file as [" << outPath << "]: ";
    std::string customOut;
    std::getline(std::cin, customOut);
    if (!customOut.empty()) outPath = customOut;

    saveEncoded(outPath, encoded);
    std::cout << "Saved to " << outPath << "\n";
}

static void decompressFile() {
    std::cout << "\n=== Decompress a File ===\n";
    std::cout << "Browse .huf files from:\n";
    std::cout << "  [1] Desktop\n";
    std::cout << "  [2] Current directory\n";
    std::cout << "  [3] Enter a custom path\n";
    std::cout << "Choice: ";

    int locChoice = 0;
    if (!(std::cin >> locChoice)) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "Invalid choice.\n";
        return;
    }
    std::cin.ignore(10000, '\n');

    std::string inputPath;
    switch (locChoice) {
        case 1: inputPath = browseAndSelectFile(getDesktopPath());    break;
        case 2: inputPath = browseAndSelectFile(fs::current_path());  break;
        case 3: {
            std::cout << "Enter directory path to browse: ";
            std::string dir;
            std::getline(std::cin, dir);
            inputPath = browseAndSelectFile(fs::path(dir));
            break;
        }
        default:
            std::cout << "Invalid choice.\n";
            return;
    }

    if (inputPath.empty()) { std::cout << "No file selected.\n"; return; }

    std::cout << "Loading " << inputPath << " ...\n";
    auto encoded = loadEncoded(inputPath);
    std::cout << "Original size was " << encoded.originalLength << " bytes\n";

    std::cout << "Decompressing ...\n";
    auto decoded = decode(encoded);

    std::string outPath = inputPath;
    if (outPath.size() > 4 && outPath.substr(outPath.size() - 4) == ".huf") {
        outPath = outPath.substr(0, outPath.size() - 4);
    } else {
        outPath += ".decoded";
    }
    std::cout << "Save decompressed file as [" << outPath << "]: ";
    std::string customOut;
    std::getline(std::cin, customOut);
    if (!customOut.empty()) outPath = customOut;

    writeFile(outPath, decoded);
    std::cout << "Saved to " << outPath << " (" << decoded.size() << " bytes)\n";
}

// ===========================================================================
// Main
// ===========================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "   Huffman File Compressor (C++)        \n";
    std::cout << "========================================\n";

    while (true) {
        std::cout << "\nMenu:\n";
        std::cout << "  [1] Compress a file\n";
        std::cout << "  [2] Decompress a file\n";
        std::cout << "  [3] Quit\n";
        std::cout << "Choice: ";

        int choice = 0;
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(10000, '\n');
            std::cout << "Invalid input.\n";
            continue;
        }
        std::cin.ignore(10000, '\n');

        switch (choice) {
            case 1:
                try { compressFile(); }
                catch (const std::exception& e) { std::cerr << "Error: " << e.what() << "\n"; }
                break;
            case 2:
                try { decompressFile(); }
                catch (const std::exception& e) { std::cerr << "Error: " << e.what() << "\n"; }
                break;
            case 3:
                std::cout << "Goodbye!\n";
                return 0;
            default:
                std::cout << "Invalid choice.\n";
                break;
        }
    }
}
