#include "MediaProbe.hpp"

#if defined(HAVE_FFMPEG)
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace pld {

long long probeDurationMs(const std::string& path) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) != 0) return -1;
    long long ms = -1;
    if (avformat_find_stream_info(fmt, nullptr) >= 0 && fmt->duration != AV_NOPTS_VALUE)
        ms = static_cast<long long>(fmt->duration) / (AV_TIME_BASE / 1000); // AV_TIME_BASE units -> ms
    avformat_close_input(&fmt);
    return ms;
}

} // namespace pld

#else

namespace pld {
long long probeDurationMs(const std::string&) { return -1; }
} // namespace pld

#endif
