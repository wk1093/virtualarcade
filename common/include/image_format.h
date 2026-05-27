#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <fstream>
#include <algorithm>

// Virtual Arcade Image Format (.img)
// This format stores ROM/RAM images with optional RLE compression
// 
// File structure:
// [MAGIC: "VIMG" - 4 bytes]
// [VERSION: 1 byte] (currently 1)
// [NUM_IMAGES: 2 bytes]
// For each image:
//   [IMAGE_NAME_LEN: 1 byte]
//   [IMAGE_NAME: N bytes]
//   [IMAGE_TYPE: 1 byte] (0=ROM, 1=RAM)
//   [UNCOMPRESSED_SIZE: 4 bytes]
//   [COMPRESSED_SIZE: 4 bytes]
//   [DATA: COMPRESSED_SIZE bytes]

namespace VarcadeImage {

static const char MAGIC[] = "VIMG";
static const uint8_t FORMAT_VERSION = 1;

enum ImageType : uint8_t {
    TYPE_ROM = 0,
    TYPE_RAM = 1
};

struct ImageHeader {
    std::string name;
    ImageType type;
    uint32_t uncompressed_size;
    uint32_t compressed_size;
};

// RLE Compression/Decompression
// Format: Uses 0xFF as marker for runs
//   0xFF 0x00 = literal 0xFF (escaped)
//   0xFF [count] [byte] = RLE run (count 1-254, byte 0-255)
//   Other bytes = literals

class RLECodec {
public:
    // Compress data with RLE encoding
    // Returns compressed data as vector
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input) {
        std::vector<uint8_t> output;
        output.reserve(input.size()); // Worst case: no compression
        
        size_t i = 0;
        while (i < input.size()) {
            uint8_t byte = input[i];
            
            // Count consecutive identical bytes
            size_t run_length = 1;
            while (i + run_length < input.size() && 
                   input[i + run_length] == byte && 
                   run_length < 254) {
                run_length++;
            }
            
            // Use RLE for runs of 4+ bytes, or special case 0xFF
            if (run_length >= 4 || (byte == 0xFF && run_length > 1)) {
                output.push_back(0xFF);
                output.push_back(static_cast<uint8_t>(run_length));
                output.push_back(byte);
                i += run_length;
            } else {
                // Output literal bytes
                for (size_t j = 0; j < run_length; j++) {
                    if (byte == 0xFF) {
                        // Escape 0xFF bytes
                        output.push_back(0xFF);
                        output.push_back(0x00);
                    } else {
                        output.push_back(byte);
                    }
                }
                i += run_length;
            }
        }
        
        return output;
    }
    
    // Decompress RLE encoded data
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input, 
                                            uint32_t uncompressed_size) {
        std::vector<uint8_t> output;
        output.reserve(uncompressed_size);
        
        size_t i = 0;
        while (i < input.size() && output.size() < uncompressed_size) {
            if (input[i] == 0xFF && i + 1 < input.size()) {
                uint8_t count = input[i + 1];
                
                if (count == 0x00) {
                    // Escaped 0xFF
                    output.push_back(0xFF);
                    i += 2;
                } else {
                    // RLE run
                    if (i + 2 < input.size()) {
                        uint8_t byte = input[i + 2];
                        for (uint8_t j = 0; j < count && output.size() < uncompressed_size; j++) {
                            output.push_back(byte);
                        }
                        i += 3;
                    } else {
                        throw std::runtime_error("Malformed RLE data: incomplete run marker");
                    }
                }
            } else {
                output.push_back(input[i]);
                i++;
            }
        }
        
        if (output.size() != uncompressed_size) {
            throw std::runtime_error("Decompressed size mismatch: got " + 
                                    std::to_string(output.size()) + 
                                    " expected " + std::to_string(uncompressed_size));
        }
        
        return output;
    }
};

// Image file handling
class ImageFile {
public:
    std::vector<ImageHeader> headers;
    std::vector<std::vector<uint8_t>> images;
    
    // Load from file
    static ImageFile load(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        
        ImageFile img;
        
        // Read magic
        char magic[4];
        file.read(magic, 4);
        if (std::string(magic, 4) != MAGIC) {
            throw std::runtime_error("Invalid image file: bad magic number");
        }
        
        // Read version
        uint8_t version;
        file.read(reinterpret_cast<char*>(&version), 1);
        if (version != FORMAT_VERSION) {
            throw std::runtime_error("Unsupported image format version: " + 
                                    std::to_string(version));
        }
        
        // Read image count
        uint16_t num_images;
        file.read(reinterpret_cast<char*>(&num_images), 2);
        
        // Read each image header and data
        for (uint16_t i = 0; i < num_images; i++) {
            ImageHeader header;
            
            // Read name
            uint8_t name_len;
            file.read(reinterpret_cast<char*>(&name_len), 1);
            header.name.resize(name_len);
            file.read(&header.name[0], name_len);
            
            // Read type
            uint8_t type;
            file.read(reinterpret_cast<char*>(&type), 1);
            header.type = static_cast<ImageType>(type);
            
            // Read sizes
            file.read(reinterpret_cast<char*>(&header.uncompressed_size), 4);
            file.read(reinterpret_cast<char*>(&header.compressed_size), 4);
            
            // Read compressed data
            std::vector<uint8_t> compressed_data(header.compressed_size);
            file.read(reinterpret_cast<char*>(compressed_data.data()), header.compressed_size);
            
            // Decompress
            std::vector<uint8_t> decompressed = RLECodec::decompress(compressed_data, 
                                                                       header.uncompressed_size);
            
            img.headers.push_back(header);
            img.images.push_back(decompressed);
        }
        
        file.close();
        return img;
    }
    
    // Save to file
    void save(const std::string& filename) const {
        if (headers.size() != images.size()) {
            throw std::runtime_error("Header/image count mismatch");
        }
        
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot create file: " + filename);
        }
        
        // Write magic
        file.write(MAGIC, 4);
        
        // Write version
        uint8_t version = FORMAT_VERSION;
        file.write(reinterpret_cast<const char*>(&version), 1);
        
        // Write image count
        uint16_t num_images = static_cast<uint16_t>(headers.size());
        file.write(reinterpret_cast<const char*>(&num_images), 2);
        
        // Write each image
        for (size_t i = 0; i < headers.size(); i++) {
            const auto& header = headers[i];
            const auto& image = images[i];
            
            // Write name
            uint8_t name_len = static_cast<uint8_t>(header.name.length());
            file.write(reinterpret_cast<const char*>(&name_len), 1);
            file.write(header.name.c_str(), name_len);
            
            // Write type
            uint8_t type = static_cast<uint8_t>(header.type);
            file.write(reinterpret_cast<const char*>(&type), 1);
            
            // Compress data
            std::vector<uint8_t> compressed = RLECodec::compress(image);
            
            // Write sizes
            uint32_t uncompressed_size = static_cast<uint32_t>(image.size());
            uint32_t compressed_size = static_cast<uint32_t>(compressed.size());
            file.write(reinterpret_cast<const char*>(&uncompressed_size), 4);
            file.write(reinterpret_cast<const char*>(&compressed_size), 4);
            
            // Write compressed data
            file.write(reinterpret_cast<const char*>(compressed.data()), compressed_size);
        }
        
        file.close();
    }
    
    // Add image
    void add_image(const std::string& name, ImageType type, 
                   const std::vector<uint8_t>& data) {
        ImageHeader header;
        header.name = name;
        header.type = type;
        header.uncompressed_size = static_cast<uint32_t>(data.size());
        header.compressed_size = 0; // Will be set on save
        
        headers.push_back(header);
        images.push_back(data);
    }
};

} // namespace VarcadeImage
