

#include <thread>
#include <vector>
#include <algorithm>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

#include "payload.hpp"
#include "interface.hpp"

#include "helpers.hpp"

using XWindow_t = Window;


struct CandidateWindowInfo{
  XWindow_t window_id;
  std::string window_name;
  int window_width;
  int window_height;
};

// https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/portal/pipewire_utils.h;drc=003b43cb850a2fb81bb482cd95381338b7205933;l=54
bool SyncDmaBuf(int fd) {
  struct dma_buf_sync sync = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
  while (true) {
    int ret;
    ret = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
    if (ret == -1 && errno == EINTR) {
      continue;
    } else if (ret == -1) {
      return false;
    } else {
      break;
    }
  }
  return true;
}

std::vector<CandidateWindowInfo> x11_sanitizer_get_targets(
  Display* display,
  XWindow_t root_window
){
  // hunt for direct children of root window that has override_redirect set to true
  Window root_return, parent_return;
  Window* children_return;
  unsigned int nchildren_return;
  auto xquery_status = XQueryTree(display, root_window, &root_return, &parent_return, &children_return, &nchildren_return);
  if (xquery_status == 0) {
    fprintf(stderr, "%s", red_text("[x11_sanitizer] XQueryTree failed. \n").c_str());
    return {};
  }
  std::vector<CandidateWindowInfo> targets;
  for (int i = 0; i < nchildren_return; i++) {
    XWindow_t cur_window = children_return[i];
    XWindowAttributes window_attributes;
    auto xgetwindow_status = XGetWindowAttributes(display, cur_window, &window_attributes);
    if (xgetwindow_status == 0) {
      fprintf(stderr, "%s", red_text("[x11_sanitizer] XGetWindowAttributes failed. \n").c_str());
      continue;
    }
    if (window_attributes.override_redirect == false) {
      continue;
    }

    // found override_redirect window
    // check its name

    std::string window_name;
    XTextProperty prop;
    Atom wmNameAtom = XInternAtom(display, "_NET_WM_NAME", True);
    if (wmNameAtom && XGetTextProperty(display, cur_window, &prop, wmNameAtom) && prop.value) {
        window_name = std::string(reinterpret_cast<char*>(prop.value));
        XFree(prop.value);
    }

    // check if "wemeet" is in the window name
    if (window_name.find("wemeet") == std::string::npos) {
      continue;
    }


    // found candidate window here, add it to the list

    targets.push_back(CandidateWindowInfo{
      .window_id = cur_window,
      .window_name = window_name,
      .window_width = window_attributes.width,
      .window_height = window_attributes.height
    });
  }
  XFree(children_return);
  return targets;
}


std::vector<std::tuple<int, int>> get_screen_sizes(
  Display* display,
  int screen
){
  std::vector<std::tuple<int, int>> screen_sizes;
  XRRScreenResources *screen_resources = XRRGetScreenResources(display, RootWindow(display, screen));
  for (int i = 0; i < screen_resources->noutput; i++) {
    RROutput output = screen_resources->outputs[i];
    XRROutputInfo *output_info = XRRGetOutputInfo(display, screen_resources, output);
    if (output_info->connection == RR_Connected) {
      XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(display, screen_resources, output_info->crtc);
      if (crtc_info) {
        screen_sizes.push_back(std::make_tuple(crtc_info->width, crtc_info->height));
        XRRFreeCrtcInfo(crtc_info);
      }
    }
    XRRFreeOutputInfo(output_info);
  }
  XRRFreeScreenResources(screen_resources);
  return screen_sizes;
}

/*
  Below is a *VERY CRAZY* code that get rid of the stupid asshole wemeet screenshare overlay window, which
  prevents the user from clicking anything on the screen.

  we accomplish this by:

  1. First locate the right window to get rid of. This can be very tricky, since wemeet would
  create and destroy multiple windows with override_redirect set to true after you started screenshare.
  If the wrong window is picked, the whole wemeet app GUI would enter a very weird state and will eventually
  crash, LMFAO. However I observed that it would eventually enter a "steady state" where the right window
  would be the one with the largest size. DerryAlex noticed this size should also be the "fullscreen size",
  though with some caveats. He also digged out that the unified strategy XUnmapWindow works well.
  
  So the eventual logic is, well, a little bit over engineered. In every 100ms, we:
    (1) first locate some candidate windows that has "wemeet" in its name and has override_redirect set to true.
    (2) then see if there is any candidate window whose size is "roughly the same" as the screen size.
    (3) if there is one, we just select it and then exit.
    (4) if there isn't one, the loop continues
  This loop will continue until $STEADY_WAIT_TIME has passed. If things turn out like this we use the fallback
  strategy that picks the window with the largest size.

  2. Just hide the window, using the unified strategy "XUnmapWindow" regardless of the DE type.
    However we keep the DE detection logic here in case we need to use different strategies for different DEs...
*/

void x11_sanitizer_main()
{
  // get the current session type
  // if it's "wayland", then the x11 sanitizer can just exit
  if (get_current_session_type() == SessionType::Wayland) {
    fprintf(stderr, "%s", green_text("[x11_sanitizer] wayland session detected. skipping x11 sanitizer. \n").c_str());
    return;
  }
  
  auto& interface_singleton = InterfaceSingleton::getSingleton();
  auto* interface_handle = interface_singleton.interface_handle.load();
  Display* display = XOpenDisplay(NULL);
  int screen = DefaultScreen(display);
  XWindow_t root_window = DefaultRootWindow(display);

  std::vector<std::tuple<int, int>> screen_sizes = get_screen_sizes(display, screen);
    
  // 2 seconds steady wait time seems to be "generally safe".
  std::chrono::milliseconds STEADY_WAIT_TIME(2000);
  std::chrono::time_point<std::chrono::high_resolution_clock> steady_start_time;
  bool target_first_occurred = false;
  bool target_managed = false;
  XWindow_t picked_window_id = 0;

  while(interface_handle->x11_sanitizer_stop_flag.load(std::memory_order_seq_cst) == false){

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto targets = x11_sanitizer_get_targets(display, root_window);
    auto now = std::chrono::high_resolution_clock::now();
    if (targets.size() != 0 && target_first_occurred == false) {
      target_first_occurred = true;
      steady_start_time = now;
    }
    
    // This can be much faster than STEADY_WAIT_TIME, say 200ms
    for (auto target: targets) {

      auto f_fuzzy_matched = [&](){
        for (auto [width, height]: screen_sizes) {
          bool matched = std::abs(target.window_width - width) < 5 && std::abs(target.window_height - height) < 5;
          if (matched) {
            return true;
          }
        }
        return false;
      };

      if (f_fuzzy_matched()) {
        auto duration = std::chrono::duration_cast<std::chrono::duration<long, std::milli>>(now - steady_start_time);
        picked_window_id = target.window_id;
        fprintf(stderr, "%s picked window 0x%lx after %ld ms.\n", green_text("[payload x11 sanitizer]").c_str(), picked_window_id, duration.count());
        now = now + STEADY_WAIT_TIME; // this cheats the following logic to skip the steady wait
        break;
      }
    }
    
    if (target_first_occurred && (now - steady_start_time) < STEADY_WAIT_TIME) {
      // wait for steady state
      continue;
    }

    if (target_first_occurred && (now - steady_start_time) >= STEADY_WAIT_TIME) {
      
      if (!picked_window_id) {
        fprintf(stderr, "%s", yellow_text("[payload x11 sanitizer] steady wait done. \n").c_str());
        auto comparator = [](const CandidateWindowInfo& a, const CandidateWindowInfo& b) {
          int64_t area_a = static_cast<int64_t>(a.window_width) * a.window_height;
          int64_t area_b = static_cast<int64_t>(b.window_width) * b.window_height;
          return area_a > area_b;
        };

        std::sort(targets.begin(), targets.end(), comparator);
        picked_window_id = targets[0].window_id;
        fprintf(stderr, "%s", yellow_text("[payload x11 sanitizer] picked window: 0x" + int_to_hexstr(picked_window_id) + "\n").c_str());
      }
      
      if (!target_managed) {
        DEType de_type = get_current_de_type();
        switch (de_type) {
          case DEType::GNOME:
          case DEType::KDE:
          case DEType::Unknown:
            // this unified strategy works well for most DEs
            XUnmapWindow(display, picked_window_id);
            break;
          case DEType::Hyprland:
            // FIXME: hyprland may need special workarounds
            XUnmapWindow(display, picked_window_id);
            break;
        }
        target_managed = true;
        break;
      }
    }  
  }
  XCloseDisplay(display);
}




std::thread payload_start_portal_gio_mainloop_thread(){
  auto& interface_singleton = InterfaceSingleton::getSingleton();
  auto* portal_handle = interface_singleton.portal_handle.load();
  // this thread is going to be trapped in the gio mainloop
  std::thread portal_gio_mainloop_thread = std::thread(
    [portal_handle](){
      g_main_loop_run(portal_handle->gio_mainloop);
    }
  );
  // wait until xdpsession is up
  // TODO: this CAN actually fail and trap the program...
  // TODO: but i think it's relatively safe to assume that the session will be up for now
  // TODO: eventually need to deal with this
  while(!portal_handle->session.load()){
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return std::move(portal_gio_mainloop_thread);
}

constexpr float PW_MAX_CALLRATE = 60.0;
constexpr int PW_MIN_CALLTIME_MS = 1000 / PW_MAX_CALLRATE;

std::thread payload_start_pipewire_thread(){
  auto& interface_singleton = InterfaceSingleton::getSingleton();
  auto* pipewire_handle = interface_singleton.pipewire_handle.load();
  // this thread is going to be trapped in the pipewire mainloop
  std::thread pipewire_thread = std::thread(
    [](){
      auto& interface_singleton = InterfaceSingleton::getSingleton();
      while (interface_singleton.interface_handle.load()->pw_stop_flag.load() == false) {
        auto* pipewire_handle = interface_singleton.pipewire_handle.load();
        pw_loop_iterate(pw_main_loop_get_loop(pipewire_handle->pw_mainloop), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(PW_MIN_CALLTIME_MS));
      }
      fprintf(stderr, "%s", green_text("[payload] pw stop signal received. pw stopped. \n").c_str());
    }
  );
  fprintf(stderr, "%s", green_text("[payload] pipewire thread started.\n").c_str());
  return std::move(pipewire_thread);
}


// this payload_main will be executed by the payload_thread, which is created in XShmAttachHook
// and this thread will be detached immediately after creation
void payload_main(){


  auto& interface_singleton = InterfaceSingleton::getSingleton();
  auto* portal_handle = interface_singleton.portal_handle.load();

  // start the gio mainloop thread
  std::thread portal_gio_mainloop_thread = payload_start_portal_gio_mainloop_thread();

  // start x11 sanitizer thread
  std::thread x11_sanitizer_thread = std::thread(x11_sanitizer_main);

  // start screencast session
  // this will get the pipewire fd into the portal object
  xdp_session_start(
    portal_handle->session.load(), NULL, NULL,
    XdpScreencastPortal::screencast_session_start_cb,
    portal_handle
  );
  
  // wait until pipewire_fd is up
  while(portal_handle->status.load() == XdpScreencastPortalStatus::kInit ) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  };

  // screencast cancelled. we stop the gio mainloop and join the gio mainloop thread
  if (portal_handle->status.load() == XdpScreencastPortalStatus::kCancelled) {
    fprintf(stderr, "%s", red_text("[payload] screencast cancelled. stop gio and join gio thread. \n").c_str());
    g_main_loop_quit(portal_handle->gio_mainloop);
    x11_sanitizer_thread.join();
    portal_gio_mainloop_thread.join();
    return;
  }

  while(portal_handle->pipewire_fd.load() == -1){
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  fprintf(stderr, "%s", green_text("[payload SYNC] pipewire_fd acquired: " + std::to_string(portal_handle->pipewire_fd.load()) + "\n").c_str());

  
  while(interface_singleton.pipewire_handle.load() == nullptr){
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  fprintf(stderr, "%s", green_text("[payload SYNC] got pipewire_handle.\n").c_str());

  // start the pipewire thread
  std::thread pipewire_thread = payload_start_pipewire_thread();

  // we can join the sanitizer thread here since the pipewire thread is up
  // which means the user has successfully started the screenshare
  interface_singleton.interface_handle.load()->x11_sanitizer_stop_flag.store(true, std::memory_order_seq_cst);
  x11_sanitizer_thread.join();
  fprintf(stderr, "%s", green_text("[payload SYNC] x11 sanitizer stopped.\n").c_str());

  pipewire_thread.join();
  interface_singleton.interface_handle.load()->payload_pw_stop_confirm.store(true, std::memory_order_seq_cst);
  fprintf(stderr, "%s", green_text("[payload SYNC] pw stop confirmed.\n").c_str());

  portal_gio_mainloop_thread.join();
  interface_singleton.interface_handle.load()->payload_gio_stop_confirm.store(true, std::memory_order_seq_cst);
  fprintf(stderr, "%s", green_text("[payload SYNC] gio stop confirmed.\n").c_str());

  
  return;

}
