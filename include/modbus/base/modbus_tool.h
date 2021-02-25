#ifndef __MODBUS_TOOL_H_
#define __MODBUS_TOOL_H_

#include "fmt/core.h"
#include "modbus_types.h"
#include <map>

namespace modbus {
class tool {
public:
  static inline std::string dumpHex(const ByteArray &byteArray,
                                    const std::string &delimiter = " ") {
    std::string hexString;
    const int size = byteArray.size();
    hexString.reserve(size * 3);
    for (const auto &ch : byteArray) {
      hexString.append(fmt::format("{}{:02x}", delimiter, ch));
    }
    return hexString;
  }

  static inline std::string dumpRaw(const ByteArray &byteArray) {
    std::string output;
    for (const auto &ch : byteArray) {
      output += ch;
    }
    return output;
  }

  static inline ByteArray fromHexString(const ByteArray &hexString) {
    static std::map<char, int> table = {
        {'0', 0},  {'1', 1},  {'2', 2},  {'3', 3},  {'4', 4},  {'5', 5},
        {'6', 6},  {'7', 7},  {'8', 8},  {'9', 9},  {'a', 10}, {'b', 11},
        {'c', 12}, {'d', 13}, {'e', 14}, {'f', 15}, {'A', 10}, {'B', 11},
        {'C', 12}, {'D', 13}, {'E', 14}, {'F', 15}};

    ByteArray array;
    for (size_t i = 0; i < hexString.size() && hexString.size() >= 2; i += 2) {
      auto first = table.find(hexString[i]);
      auto second = table.find(hexString[i + 1]);
      if (first == table.end() || second == table.end()) {
        break;
      }
      char ch = 0;
      ch = first->second << 4 | second->second;
      array.push_back(ch);
    }
    return array;
  }

  static uint16_t crc16_modbus(const uint8_t *data, size_t size);
  static uint8_t lrc_modbus(const uint8_t *data, size_t len);
  /**
   * Calculate the crc check of data and then return data+crc.
The crc check is added in the order of the first low order and the high
order.
   */
  static inline ByteArray appendCrc(const ByteArray &data) {
    uint16_t crc = crc16_modbus((uint8_t *)(data.data()), data.size());
    auto dataWithCrc = data;
    /// first push low bit
    dataWithCrc.push_back(crc % 256);
    /// second push high bit
    dataWithCrc.push_back(crc / 256);
    return dataWithCrc;
  }

  static inline ByteArray appendLrc(const ByteArray &data) {
    uint8_t lrc = lrc_modbus((uint8_t *)data.data(), data.size());
    auto dataWithLrc = data;
    dataWithLrc.push_back(lrc);

    return dataWithLrc;
  }

  static inline ByteArray subArray(const ByteArray &array, size_t index,
                                   int n = -1) {
    if (n == -1) {
      return ByteArray(array.begin() + index, array.end());
    } else {
      return ByteArray(array.begin() + index, array.begin() + index + n);
    }
  }
}; // namespace modbus

} // namespace modbus

#endif // __MODBUS_TOOL_H_
