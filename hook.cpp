
#include <string>
#include <thread>
#include <tuple>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// got to include this before X11 headers
#include "hook_opencv.hpp"

#include <X11/Xlib.h>

#include "framebuf.hpp"
#include "format_conversion.hpp"
#include "payload.hpp"
#include "interface.hpp"
#include "helpers.hpp"
#include "hook.hpp"

/*

  Why I'm using STB here:
  Initially I tried to use opencv as I actually have worked with it before
  However for *some* reason long as the opencv is linked to the hook
  the hook would always crash wemeetapp. I suspect that maybe the wemeetapp
  itself is also using opencv and thereby some conflict arises.

  I also tried the CImg library but its in-memory image layout is just ABSURD,
  which could cause severe performance issue.

  So I eventually picked STB, and I'm happy with it now.

*/

// #define STB_IMAGE_RESIZE_IMPLEMENTATION
// #include <stb/stb_image_resize2.h>

constexpr uint32_t DEFAULT_FRAME_HEIGHT = 1080;
constexpr uint32_t DEFAULT_FRAME_WIDTH = 1920;

void XShmAttachHook(){

  auto& interface_singleton = InterfaceSingleton::getSingleton();

  // initialize interface singleton:
  // (1) allocate the interface object
  interface_singleton.interface_handle = new Interface(
    DEFAULT_FB_ALLOC_HEIGHT, DEFAULT_FB_ALLOC_WIDTH,
    DEFAULT_FRAME_HEIGHT, DEFAULT_FRAME_WIDTH, SpaVideoFormat_e::RGBA
  );
  // (2) allocate the screencast portal object
  interface_singleton.portal_handle = new XdpScreencastPortal();

  // start the payload thread
  std::thread payload_thread = std::thread(payload_main);
  fprintf(stderr, "%s", green_text("[hook] payload thread started\n").c_str());

  while(interface_singleton.portal_handle.load()->status.load(std::memory_order_seq_cst) == XdpScreencastPortalStatus::kInit ) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  };
  auto payload_status = interface_singleton.portal_handle.load()->status.load(std::memory_order_seq_cst);
  std::string payload_status_str = 
    payload_status == XdpScreencastPortalStatus::kCancelled ? "cancelled" :
    payload_status == XdpScreencastPortalStatus::kRunning ? "running" :
    "unknown";
  if (payload_status == XdpScreencastPortalStatus::kRunning) {
    // things are good
    fprintf(stderr, "%s", green_text("[hook] portal status: " + payload_status_str + "\n").c_str());
  } else {
    // things are bad, we have to de-initialize and exit
    fprintf(stderr, "%s", red_text("[hook] portal status: " + payload_status_str + "\n").c_str());
    fprintf(stderr, "%s", red_text("[hook] <<<!!Please DO NOT cancel screencast!!>> Hook is now exiting.\n").c_str());
    // payload thread should have quitted via g_main_loop_quit
    payload_thread.join();
    delete interface_singleton.interface_handle.load();
    delete interface_singleton.portal_handle.load();
    interface_singleton.interface_handle.store(nullptr);
    interface_singleton.portal_handle.store(nullptr);
    return;
  }


  while(interface_singleton.portal_handle.load()->pipewire_fd.load(std::memory_order_seq_cst) == -1){
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  fprintf(stderr, "%s", green_text("[hook SYNC] pipewire_fd acquired: " + std::to_string(interface_singleton.portal_handle.load()->pipewire_fd.load()) + "\n").c_str());

  interface_singleton.pipewire_handle = new PipewireScreenCast(interface_singleton.portal_handle.load()->pipewire_fd.load(), interface_singleton.portal_handle.load()->pipewire_node_ids.at(0));
  fprintf(stderr, "%s", green_text("[hook SYNC] pipewire screencast object allocated\n").c_str());
  
  payload_thread.detach();

}

template <typename T>
struct remove_pointer_cvref {
  using type = std::remove_cv_t<std::remove_pointer_t<std::remove_reference_t<T>>>;
};

template <typename T>
using remove_pointer_cvref_t = typename remove_pointer_cvref<T>::type;

void XShmGetImageHook(XImage& image){
  auto& interface_singleton = InterfaceSingleton::getSingleton();

  if (interface_singleton.interface_handle.load() == nullptr){
    fprintf(stderr, "%s", red_text("[hook] hook will NOT work as you have cancelled the screencast!!!\n").c_str());
    return;
  }

  auto ximage_spa_format = ximage_to_spa(image);
  auto ximage_width = image.width;
  auto ximage_height = image.height;
  size_t ximage_bytes_per_line = image.bytes_per_line;

  CvMat ximage_cvmat;
  OpencvDLFCNSingleton::cvInitMatHeader(
    &ximage_cvmat, ximage_height, ximage_width,
    CV_8UC4, image.data, ximage_bytes_per_line
  );

  auto& framebuffer = interface_singleton.interface_handle.load()->framebuf;
  PopulateImageMat(framebuffer, ximage_cvmat, ximage_spa_format);
}


void XShmDetachStopPWLoop(){
  auto& interface_singleton = InterfaceSingleton::getSingleton();
  fprintf(stderr, "%s", green_text("[hook] signal pw stop.\n").c_str());
  interface_singleton.interface_handle.load()->pw_stop_flag.store(true, std::memory_order_seq_cst);
  while(!interface_singleton.interface_handle.load()->payload_pw_stop_confirm.load(std::memory_order_seq_cst)){
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  fprintf(stderr, "%s", green_text("[hook SYNC] pw stop confirmed.\n").c_str());
  return;
}

void XShmDetachStopGIOLoop(){
  auto& interface_singleton = InterfaceSingleton::getSingleton();
  fprintf(stderr, "%s", green_text("[hook] stop gio main loop.\n").c_str());
  g_main_loop_quit(interface_singleton.portal_handle.load()->gio_mainloop);
  while(!interface_singleton.interface_handle.load()->payload_gio_stop_confirm.load(std::memory_order_seq_cst)){
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  fprintf(stderr, "%s", green_text("[hook SYNC] gio stop confirmed.\n").c_str());
  return;
}

void XShmDetachHook(){

  auto& interface_singleton = InterfaceSingleton::getSingleton();
  
  // the interface_handle being non nullptr
  // means that the screencast has been started
  if (interface_singleton.interface_handle != nullptr){

    XShmDetachStopPWLoop();
    XShmDetachStopGIOLoop();

    // normal de-initialize interface singleton:
    // (1) free the interface object
    delete interface_singleton.interface_handle.load();
    interface_singleton.interface_handle.store(nullptr);
    // (2) free the screencast portal object
    delete interface_singleton.portal_handle.load();
    interface_singleton.portal_handle.store(nullptr);
    // (3) free the pipewire screencast object
    delete interface_singleton.pipewire_handle.load();
    interface_singleton.pipewire_handle.store(nullptr);
  } else {
    // we do nothing here since the objects (interface, portal) are already freed,
    // and pipewire object has never been created
    fprintf(stderr, "%s", red_text("[hook] objects are already freed because of cancelled screencast. exiting.\n").c_str());
  }
  

}

extern "C" {

Bool XShmAttach(Display* dpy, XShmSegmentInfo* shminfo){
  XShmAttachHook();
  return XShmAttachFunc(dpy, shminfo);
}

Bool XShmGetImage(Display* dpy, Drawable d, XImage* image, int x, int y, unsigned long plane_mask){
  XShmGetImageHook(*image);
  return 1;
}

Bool XShmDetach(Display* dpy, XShmSegmentInfo* shminfo){
  XShmDetachHook();
  return XShmDetachFunc(dpy, shminfo);
}

Bool XDamageQueryExtension(Display *dpy, int *event_base_return, int *error_base_return) {
  return 0;
}

}
