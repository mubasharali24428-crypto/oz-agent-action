#include "huffman.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Utility: list files in a directory and let the user pick one.
// ---------------------------------------------------------------------------

/**
 * List files in `directory`, display them with 1-based indices, and return
 * the full path chosen by the user.  Returns an empty string if the user
 * cancels or the directory is empty.
 */
static std::string browseAndSelectFile(const fs::path& directory) {
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        std::cerr << "Directory does not exist: " << directory << "\n";
        return {};
    }

    // Collect regular files (non-hidden, one level deep).
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

/** Try to locate the user's Desktop directory. */
static fs::path getDesktopPath() {
    // Check common environment variables first.
    if (const char* desktop = std::getenv("XDG_DESKTOP_DIR")) {
        return fs::path(desktop);
    }
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home) / "Desktop";
    }
    if (const char* userprofile = std::getenv("USERPROFILE")) {
        return fs::path(userprofile) / "Desktop";
    }
    // Fallback: current working directory.
    return fs::current_path();
}

// ---------------------------------------------------------------------------
// Read an entire file into a byte vector.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Menu actions
// ---------------------------------------------------------------------------

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
        case 1:
            inputPath = browseAndSelectFile(getDesktopPath());
            break;
        case 2:
            inputPath = browseAndSelectFile(fs::current_path());
            break;
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

    if (inputPath.empty()) {
        std::cout << "No file selected.\n";
        return;
    }

    std::cout << "Reading " << inputPath << " ...\n";
    auto data = readFile(inputPath);
    std::cout << "Original size: " << data.size() << " bytes\n";

    std::cout << "Compressing ...\n";
    auto encoded = encode(data);
    std::cout << "Compressed to " << encoded.data.size() << " bytes ("
              << encoded.codes.size() << " unique symbols)\n";

    // Default output path: same name with .huf extension.
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
        case 1:
            inputPath = browseAndSelectFile(getDesktopPath());
            break;
        case 2:
            inputPath = browseAndSelectFile(fs::current_path());
            break;
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

    if (inputPath.empty()) {
        std::cout << "No file selected.\n";
        return;
    }

    std::cout << "Loading " << inputPath << " ...\n";
    auto encoded = loadEncoded(inputPath);
    std::cout << "Original size was " << encoded.originalLength << " bytes\n";

    std::cout << "Decompressing ...\n";
    auto decoded = decode(encoded);

    // Default output path: remove .huf extension if present.
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

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------

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
                try {
                    compressFile();
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << "\n";
                }
                break;
            case 2:
                try {
                    decompressFile();
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << "\n";
                }
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
