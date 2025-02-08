#pragma once

#include <string>
#include <spa/param/video/format-utils.h>

enum class SpaVideoFormat_e {
  RGBx = SPA_VIDEO_FORMAT_RGBx,
  BGRx = SPA_VIDEO_FORMAT_BGRx,
  RGBA = SPA_VIDEO_FORMAT_RGBA,
  BGRA = SPA_VIDEO_FORMAT_BGRA,
  RGB = SPA_VIDEO_FORMAT_RGB,
  BGR = SPA_VIDEO_FORMAT_BGR,
  INVALID = -1
};

inline std::string spa_to_string(SpaVideoFormat_e const& format){
  switch (format) {
    case SpaVideoFormat_e::RGBx:
      return "RGBx";
    case SpaVideoFormat_e::BGRx:
      return "BGRx";
    case SpaVideoFormat_e::RGBA:
      return "RGBA";
    case SpaVideoFormat_e::BGRA:
      return "BGRA";
    case SpaVideoFormat_e::RGB:
      return "RGB";
    case SpaVideoFormat_e::BGR:
      return "BGR";
    default:
      return "INVALID";
  }
}

inline auto spa_videoformat_bytesize(const SpaVideoFormat_e& format) -> int {
  switch (format) {
    case SpaVideoFormat_e::RGBx:
      return 4;
    case SpaVideoFormat_e::BGRx:
      return 4;
    case SpaVideoFormat_e::RGBA:
      return 4;
    case SpaVideoFormat_e::BGRA:
      return 4;
    case SpaVideoFormat_e::RGB:
      return 3;
    case SpaVideoFormat_e::BGR:
      return 3;
    default:
      return -1;
  }
}
