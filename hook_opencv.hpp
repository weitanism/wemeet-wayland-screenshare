#pragma once

#include <cstdio>

#include <stdexcept>
#include <dlfcn.h>

#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/imgproc_c.h>

struct OpencvDLFCNSingleton{

  using THIS_CLASS = OpencvDLFCNSingleton;

private:
  OpencvDLFCNSingleton(){
    libopencv_core_handle = dlopen("libopencv_core.so", RTLD_LOCAL|RTLD_LAZY);
    if(libopencv_core_handle == nullptr){
      throw std::runtime_error("Failed to open library libopencv_core.so");
    }
    libopencv_imgproc_handle = dlopen("libopencv_imgproc.so", RTLD_LOCAL|RTLD_LAZY);
    if(libopencv_imgproc_handle == nullptr){
      throw std::runtime_error("Failed to open library libopencv_imgproc.so");
    }
    libopencv_imgcodecs_handle = dlopen("libopencv_imgcodecs.so", RTLD_LOCAL|RTLD_LAZY);
    if(libopencv_imgcodecs_handle == nullptr){
      throw std::runtime_error("Failed to open library libopencv_imgcodecs.so");
    }
  }

public:

  inline static OpencvDLFCNSingleton& getSingleton(){
    static OpencvDLFCNSingleton singleton;
    return singleton;
  }

  ~OpencvDLFCNSingleton(){
    if (libopencv_core_handle != nullptr){
      dlclose(libopencv_core_handle);
    }
    if (libopencv_imgproc_handle != nullptr){
      dlclose(libopencv_imgproc_handle);
    }
  }

  static inline CvMat* cvInitMatHeader(
    CvMat* mat, int rows, int cols,
    int type, void* data = NULL,
    int step = CV_AUTOSTEP
  ){
    using FType = CvMat*(CvMat*, int, int, int, void*, int);
    auto& singleton = getSingleton();
    static auto func = (FType*)dlsym(singleton.libopencv_core_handle, "cvInitMatHeader");
    return func(mat, rows, cols, type, data, step);
  }

  static inline CvMat* cvCreateMat(
    int rows, int cols, int type
  ){
    using FType = CvMat*(int, int, int);
    auto& singleton = getSingleton();
    static auto func = (FType*)dlsym(singleton.libopencv_core_handle, "cvCreateMat");
    return func(rows, cols, type);
  }
  
  static inline void cvReleaseMat(CvMat** mat){
    using FType = void(CvMat**);
    auto& singleton = getSingleton();
    static auto func = (FType*)dlsym(singleton.libopencv_core_handle, "cvReleaseMat");
    return func(mat);
  }

  static inline CvMat* cvGetSubRect(
    const CvArr* arr, CvMat* submat, CvRect rect
  ){
    using FType = CvMat*(const CvArr*, CvMat*, CvRect);
    auto& singleton = getSingleton();
    static auto func = (FType*)dlsym(singleton.libopencv_core_handle, "cvGetSubRect");
    return func(arr, submat, rect);
  }

  static inline void cvResize(
    const CvArr* src, CvArr* dst,
    int interpolation
  ){
    using FType = void(const CvArr*, CvArr*, int);
    auto& singleton = getSingleton();
    static auto func = (FType*)dlsym(singleton.libopencv_imgproc_handle, "cvResize");
    return func(src, dst, interpolation);
  }

  static inline void cvSetZero(CvArr* arr){
    using FType = void(CvArr*);
    auto& singleton = getSingleton();
    static auto func = (FType*)dlsym(singleton.libopencv_core_handle, "cvSetZero");
    return func(arr);
  }

  static inline void cvCopy(const CvArr* src, CvArr* dst){
    using FType = void(const CvArr*, CvArr*);
    auto& singleton = getSingleton();
    static auto func = (FType*)dlsym(singleton.libopencv_core_handle, "cvCopy");
    return func(src, dst);
  }

  static inline void cvCvtColor(
    const CvArr* src, CvArr* dst,
    int code
  ){
    using FType = void(const CvArr*, CvArr*, int);
    auto& singleton = getSingleton();
    static auto func = (FType*)dlsym(singleton.libopencv_imgproc_handle, "cvCvtColor");
    return func(src, dst, code);
  }

  static inline int cvSaveImage(
    const char* filename, const CvArr* image,
    const int* params = 0
  ){
    using FType = int(const char*, const CvArr*, const int*);
    auto& singleton = getSingleton();
    static auto func = (FType*)dlsym(singleton.libopencv_imgcodecs_handle, "cvSaveImage");
    if (func == nullptr) {
      return -1;
    }
    return func(filename, image, params);
  }

  void* libopencv_core_handle{nullptr};
  void* libopencv_imgproc_handle{nullptr};
  void* libopencv_imgcodecs_handle{nullptr};


};
