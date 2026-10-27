// Implementação do decodificador PNG mínimo (zlib + filtros de linha).
#include "png_loader.h"

#include <zlib.h>

#include <cstring>
#include <fstream>

namespace loomia {

namespace {

uint32_t readU32(const std::vector<uint8_t>& data, size_t offset) {
  if (offset + 4 > data.size()) return 0;
  return (static_cast<uint32_t>(data[offset]) << 24) | (static_cast<uint32_t>(data[offset + 1]) << 16) |
         (static_cast<uint32_t>(data[offset + 2]) << 8) | static_cast<uint32_t>(data[offset + 3]);
}

uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
  const int p = a + b - c;
  const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

}  // namespace

PngImage decodePng(const std::vector<uint8_t>& bytes, std::string& error) {
  PngImage out;
  // Assinatura
  const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (bytes.size() < 8 || std::memcmp(bytes.data(), signature, 8) != 0) {
    error = "não é um PNG";
    return out;
  }

  size_t offset = 8;
  int width = 0, height = 0, bitDepth = 0, colorType = 0, interlace = 0;
  std::vector<uint8_t> idat;
  std::vector<uint8_t> palette;  // RGB triplets
  bool sawIhdr = false;

  while (offset + 8 <= bytes.size()) {
    const uint32_t length = readU32(bytes, offset);
    const char* type = reinterpret_cast<const char*>(&bytes[offset + 4]);
    const size_t dataStart = offset + 8;
    if (dataStart + length + 4 > bytes.size()) break;

    if (std::memcmp(type, "IHDR", 4) == 0) {
      width = static_cast<int>(readU32(bytes, dataStart));
      height = static_cast<int>(readU32(bytes, dataStart + 4));
      bitDepth = bytes[dataStart + 8];
      colorType = bytes[dataStart + 9];
      interlace = bytes[dataStart + 12];
      sawIhdr = true;
    } else if (std::memcmp(type, "PLTE", 4) == 0) {
      palette.assign(bytes.begin() + dataStart, bytes.begin() + dataStart + length);
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      idat.insert(idat.end(), bytes.begin() + dataStart, bytes.begin() + dataStart + length);
    } else if (std::memcmp(type, "IEND", 4) == 0) {
      break;
    }
    offset = dataStart + length + 4;  // + CRC
  }

  if (!sawIhdr || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
    error = "PNG com IHDR inválido";
    return out;
  }
  if (bitDepth != 8) {
    error = "PNG com bit depth " + std::to_string(bitDepth) + " não suportado (só 8)";
    return out;
  }
  if (interlace != 0) {
    error = "PNG entrelaçado não suportado";
    return out;
  }

  int channels = 0;
  switch (colorType) {
    case 0: channels = 1; break;  // gray
    case 2: channels = 3; break;  // RGB
    case 3: channels = 1; break;  // palette (índice)
    case 4: channels = 2; break;  // gray+alpha
    case 6: channels = 4; break;  // RGBA
    default:
      error = "color type " + std::to_string(colorType) + " não suportado";
      return out;
  }

  // Descomprime com zlib.
  uLongf decodedSize = static_cast<uLongf>(height * (1 + static_cast<size_t>(width) * channels));
  std::vector<uint8_t> raw(decodedSize);
  if (uncompress(raw.data(), &decodedSize, idat.data(), static_cast<uLong>(idat.size())) != Z_OK) {
    error = "falha ao descomprimir os dados da imagem";
    return out;
  }

  // Desfaz os filtros de linha.
  const size_t stride = static_cast<size_t>(width) * channels;
  std::vector<uint8_t> pixels(height * stride);
  for (int y = 0; y < height; ++y) {
    const uint8_t filter = raw[static_cast<size_t>(y) * (stride + 1)];
    const uint8_t* lineIn = &raw[static_cast<size_t>(y) * (stride + 1) + 1];
    uint8_t* lineOut = &pixels[static_cast<size_t>(y) * stride];
    for (size_t x = 0; x < stride; ++x) {
      const uint8_t a = x >= static_cast<size_t>(channels) ? lineOut[x - channels] : 0;
      const uint8_t b = y > 0 ? pixels[static_cast<size_t>(y - 1) * stride + x] : 0;
      const uint8_t c = (x >= static_cast<size_t>(channels) && y > 0)
                            ? pixels[static_cast<size_t>(y - 1) * stride + x - channels]
                            : 0;
      uint8_t value = lineIn[x];
      switch (filter) {
        case 1: value += a; break;          // Sub
        case 2: value += b; break;          // Up
        case 3: value += static_cast<uint8_t>((a + b) / 2); break;  // Average
        case 4: value += paeth(a, b, c); break;  // Paeth
        default: break;                     // None
      }
      lineOut[x] = value;
    }
  }

  // Converte para RGBA.
  out.width = width;
  out.height = height;
  out.rgba.resize(static_cast<size_t>(width) * height * 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint8_t* src = &pixels[static_cast<size_t>(y) * stride + static_cast<size_t>(x) * channels];
      uint8_t* dst = &out.rgba[(static_cast<size_t>(y) * width + x) * 4];
      switch (colorType) {
        case 0: dst[0] = dst[1] = dst[2] = src[0]; dst[3] = 255; break;
        case 2: dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255; break;
        case 3: {
          const int index = src[0] * 3;
          dst[0] = palette[index];
          dst[1] = palette[index + 1];
          dst[2] = palette[index + 2];
          dst[3] = 255;
          break;
        }
        case 4: dst[0] = dst[1] = dst[2] = src[0]; dst[3] = src[1]; break;
        case 6: dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3]; break;
      }
    }
  }
  out.ok = true;
  return out;
}

PngImage loadPngFile(const std::string& path, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "não consegui abrir " + path;
    return {};
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return decodePng(bytes, error);
}

}  // namespace loomia
