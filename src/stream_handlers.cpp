#include "stream_handlers.hpp"
#include <optional>

bool LineStream::receive(std::span<const std::byte> data) {
    if (data.empty() || buffer.size() + data.size() > MAX_BUFFER_SIZE) {
        return data.empty();
    }
    
    buffer.reserve(buffer.size() + data.size());
    buffer.append(reinterpret_cast<const char*>(data.data()), data.size());
    
    for (const auto& byte : data) {
        if (in.sputc(static_cast<char>(byte)) == std::char_traits<char>::eof()) {
            break;
        }
    }
    
    return true;
}

std::optional<std::string> LineStream::get_line() {
    auto line_pos = buffer.find('\n', pos);
    if (line_pos == std::string::npos) {
        pos = buffer_size();
        return std::nullopt;
    }


    size_t line_len = std::min(line_pos + 1, MAX_LINE_LENGTH);
    std::string line = buffer.substr(0, line_len);
    buffer.erase(0, line_len + 1);
    
    pos = 0;
    return line;
}


