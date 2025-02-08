#pragma once

#include <cstdint>
#include <tuple>

#include <opencv2/imgproc/imgproc_c.h>
#include <X11/Xlib.h>

#include "format.hpp"
#include "framebuf.hpp"

// A VERY tedious color convert code getter
// This can be handled more gracefully using something like string matching...
// however let it be it for now
// note: -1 means no conversion is needed
inline int get_opencv_cAPI_color_convert_code(
  SpaVideoFormat_e const& src_format,
  SpaVideoFormat_e const& dst_format
){
  // shortcut1: src and dst match exactly
  if (src_format == dst_format) {
    return -1;
  }
  // shortcut2: RGBA == RGBx
  if (src_format == SpaVideoFormat_e::RGBA && dst_format == SpaVideoFormat_e::RGBx ||
      src_format == SpaVideoFormat_e::RGBx && dst_format == SpaVideoFormat_e::RGBA
  ) {
    return -1;
  }
  // shortcut3: BGRA == BGRx
  if (src_format == SpaVideoFormat_e::BGRA && dst_format == SpaVideoFormat_e::BGRx ||
      src_format == SpaVideoFormat_e::BGRx && dst_format == SpaVideoFormat_e::BGRA
  ) {
    return -1;
  }
  if (src_format == SpaVideoFormat_e::RGB){
    // RGB -> BGR
    if (dst_format == SpaVideoFormat_e::BGR){
      return CV_RGB2BGR;
    }
    // RGB -> BGRA / BGRx
    if (dst_format == SpaVideoFormat_e::BGRA || dst_format == SpaVideoFormat_e::BGRx){
      return CV_RGB2BGRA;
    }
    // RGB -> RGBA / RGBx
    if (dst_format == SpaVideoFormat_e::RGBA || dst_format == SpaVideoFormat_e::RGBx){
      return CV_RGB2RGBA;
    }
  }

  if (src_format == SpaVideoFormat_e::BGR){
    // BGR -> RGB
    if (dst_format == SpaVideoFormat_e::RGB){
      return CV_BGR2RGB;
    }
    // BGR -> BGRA / BGRx
    if (dst_format == SpaVideoFormat_e::BGRA || dst_format == SpaVideoFormat_e::BGRx){
      return CV_BGR2BGRA;
    }
    // BGR -> RGBA / RGBx
    if (dst_format == SpaVideoFormat_e::RGBA || dst_format == SpaVideoFormat_e::RGBx){
      return CV_BGR2RGBA;
    }
  }

  if (src_format == SpaVideoFormat_e::RGBA || src_format == SpaVideoFormat_e::RGBx){
    // RGBA/RGBx -> RGB
    if (dst_format == SpaVideoFormat_e::RGB){
      return CV_RGBA2RGB;
    }
    // RGBA/RGBx -> BGR
    if (dst_format == SpaVideoFormat_e::BGR){
      return CV_RGBA2BGR;
    }
    // RGBA/RGBx -> BGRA/BGRx
    if (dst_format == SpaVideoFormat_e::BGRA || dst_format == SpaVideoFormat_e::BGRx){
      return CV_RGBA2BGRA;
    }
  }

  if (src_format == SpaVideoFormat_e::BGRA || src_format == SpaVideoFormat_e::BGRx){
    // BGRA/BGRx -> RGB
    if (dst_format == SpaVideoFormat_e::RGB){
      return CV_BGRA2RGB;
    }
    // BGRA/BGRx -> BGR
    if (dst_format == SpaVideoFormat_e::BGR){
      return CV_BGRA2BGR;
    }
    // BGRA/BGRx -> RGBA/RGBx
    if (dst_format == SpaVideoFormat_e::RGBA || dst_format == SpaVideoFormat_e::RGBx){
      return CV_BGRA2RGBA;
    }
  }

  // guard
  return -1;
}

inline auto ximage_to_spa(const XImage& ximage) -> SpaVideoFormat_e {
  if (ximage.format != 2){
    // we only support ZPixmap
    return SpaVideoFormat_e::INVALID;
  }
  if (ximage.bits_per_pixel == 32) {
    // possibly RGBA, BGRA, RGBx or BGRx
    // we just combine RGBx and BGRx to RGBA and BGRA, respectively
    if (ximage.red_mask == 0xff0000 && ximage.green_mask == 0xff00 && ximage.blue_mask == 0xff) {
      return SpaVideoFormat_e::BGRA;
    } else if (ximage.red_mask == 0xff && ximage.green_mask == 0xff00 && ximage.blue_mask == 0xff0000) {
      return SpaVideoFormat_e::RGBA;
    } else {
      return SpaVideoFormat_e::INVALID;
    }
  } else {
    // possibly RGB or BGR
    if (ximage.red_mask == 0xff0000 && ximage.green_mask == 0xff00 && ximage.blue_mask == 0xff) {
      return SpaVideoFormat_e::BGR;
    } else if (ximage.red_mask == 0xff && ximage.green_mask == 0xff00 && ximage.blue_mask == 0xff0000) {
      return SpaVideoFormat_e::RGB;
    } else {
      return SpaVideoFormat_e::INVALID;
    }
  }
    
}

// returns: ximage_width_offset, ximage_height_offset, target_width, target_height
inline std::tuple<uint32_t, uint32_t, uint32_t, uint32_t> get_resize_param(
  uint32_t ximage_width,
  uint32_t ximage_height,
  uint32_t framebuffer_width,
  uint32_t framebuffer_height
){
  // keep the framebuffer aspect ratio
  double framebuffer_aspect_ratio = static_cast<double>(framebuffer_width) / static_cast<double>(framebuffer_height);
  double ximage_aspect_ratio = static_cast<double>(ximage_width) / static_cast<double>(ximage_height);

  uint32_t target_width = 0;
  uint32_t target_height = 0;
  uint32_t ximage_width_offset = 0;
  uint32_t ximage_height_offset = 0;

  if (framebuffer_aspect_ratio > ximage_aspect_ratio) {
    // framebuffer is wider than ximage
    target_width = ximage_width;
    target_height = (ximage_width * framebuffer_height) / framebuffer_width;
    ximage_height_offset = (ximage_height - target_height) / 2;
  } else {
    // framebuffer is taller than ximage
    target_height = ximage_height;
    target_width = ximage_height * framebuffer_width / framebuffer_height;
    ximage_width_offset = (ximage_width - target_width) / 2;
  }

  return std::make_tuple(ximage_width_offset, ximage_height_offset, target_width, target_height);
}

inline void PopulateImageMat(const FrameBuffer& framebuffer, CvMat& cvmat, SpaVideoFormat_e format) {
  OpencvDLFCNSingleton::cvSetZero(&cvmat);

  auto framebuffer_spa_format = framebuffer.format;
  auto framebuffer_width = framebuffer.width;
  auto framebuffer_height = framebuffer.height;
  auto framebuffer_row_byte_stride = framebuffer.row_byte_stride;

  CvMat framebuffer_cvmat;
  OpencvDLFCNSingleton::cvInitMatHeader(
    &framebuffer_cvmat, framebuffer_height, framebuffer_width,
    CV_8UC4, framebuffer.data.get(), framebuffer_row_byte_stride
  );
  
  // get the resize parameters
  auto [ximage_width_offset, ximage_height_offset, target_width, target_height] = get_resize_param(
    cvmat.width, cvmat.height, framebuffer_width, framebuffer_height
  );
  CvMat ximage_cvmat_roi;
  OpencvDLFCNSingleton::cvGetSubRect(
    &cvmat, &ximage_cvmat_roi,
    cvRect(ximage_width_offset, ximage_height_offset, target_width, target_height)
  );
  OpencvDLFCNSingleton::cvResize(
    &framebuffer_cvmat, &ximage_cvmat_roi, CV_INTER_LINEAR
  );

  // do color convert
  // here the code is currently mainly for wlroot WMs
  // maybe we could shortcut this by detecting WM?

  int cv_cAPI_color_cvt_code = get_opencv_cAPI_color_convert_code(
    framebuffer_spa_format, format
  );

  if (cv_cAPI_color_cvt_code != -1){
    // non -1 code means color conversion is needed
    OpencvDLFCNSingleton::cvCvtColor(
      &ximage_cvmat_roi, &ximage_cvmat_roi, cv_cAPI_color_cvt_code
    );
  }
}
