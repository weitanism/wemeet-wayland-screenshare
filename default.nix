{
  stdenv,
  ninja,
  cmake,
  pkg-config,
  glib,
  libportal,
  pipewire,
  libX11,
  libXrandr,
  libXext,
  libXdamage,
  libXcomposite,
  opencv,
  libdrm,
  xwaylandvideobridge,
  wireplumber,
}:

stdenv.mkDerivation {
  pname = "wemeet-wayland-screenshare";
  version = "0.1";

  src = ./.;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
  ];

  buildInputs = [
    glib
    libportal
    pipewire
    libX11
    libXrandr
    libXext
    libXdamage
    libXcomposite
    opencv
    libdrm
    xwaylandvideobridge
    wireplumber
  ];

  runtimeDependencies = [
    opencv
  ];

  postFixup = ''
    patchelf $out/lib/wemeet/libhook.so --add-rpath ${opencv.out}/lib
  '';
}
