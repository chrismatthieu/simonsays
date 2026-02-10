# Simon Says

Only the **enrolled player** can make the stick man dance. This app uses the **Intel RealSense ID** SDK for:

1. **Face enrollment** – register your face on the device  
2. **Face authentication** – verify you are the enrolled user  
3. **Pose estimation** – skeletal tracking (17 COCO keypoints) to drive a stick figure on screen  

After you authenticate, the app switches the device to pose-only mode and renders your skeleton in a window. If you don’t authenticate, the stick man doesn’t appear.

## Requirements

- **RealSense ID camera** (e.g. F450/F460) connected via USB (shows as a COM port on Windows)
- **RealSense ID SDK** at the path used in `CMakeLists.txt` (default: `C:\Users\cmatthie\Documents\SDK_2.7.3.0701_471615c_Standard`)
- **CMake** 3.14+
- **C++17** compiler (e.g. Visual Studio 2019/2022 with Desktop C++ workload)
- **SDL2** (optional but recommended for the stick man window); if not found, the app builds in console-only mode

## Build

1. Install [SDL2](https://www.libsdl.org/) (e.g. via vcpkg: `vcpkg install sdl2:x64-windows`, or download development libraries and set `SDL2_DIR` or ensure CMake can find it).

2. From the project root:

   ```bat
   mkdir build
   cd build
   cmake .. -G "Visual Studio 17 2022" -A x64 ^
     -DRSID_SDK_PATH="C:/Users/cmatthie/Documents/SDK_2.7.3.0701_471615c_Standard"
   cmake --build . --config Release
   ```

   If the SDK is in a different location, set `RSID_SDK_PATH` to that path. The executable will be in `build\Release\simonsays.exe` (or `build\bin\Release\simonsays.exe` depending on the SDK layout).

3. **COM port**: The app uses the RealSense ID device on a serial port. Default on Windows is `COM9`. To use another port, set the environment variable before running:

   ```bat
   set RSID_PORT=COM5
   simonsays.exe
   ```

   On Linux the default is `/dev/ttyACM0`; set `RSID_PORT` to the correct tty if needed.

## Run

1. Connect the RealSense ID device and note its COM port (e.g. in Device Manager under “Ports (COM & LPT)”).
2. Run `simonsays.exe`.
3. When prompted **Enroll a face? (y/n)**:
   - **y**: Enroll a new user (follow the on-screen pose hints: center, up, down, left, right).
   - **n**: Skip enrollment (use an already enrolled face).
4. **Authenticate**: Stand in front of the camera. The app runs face recognition once. If it succeeds, you’re “in.”
5. **Stick man**: The window shows your pose as a stick figure. Move to make it dance. Close the window or press **Escape** to exit.

Only after a successful authentication does the stick man appear; otherwise the app exits with “Only enrolled users can play.”

## Flow summary

- **Enroll** → face stored on device under user id `player1`  
- **Authenticate** → one-shot face match; on success, app sets device to **PoseEstimationOnly**  
- **AuthenticateLoop** (pose mode) → callbacks deliver skeleton frames; app draws the stick man in the SDL window  

Pose data uses the device’s 1920×1080 coordinate space and is scaled to the 640×480 window.

## License

This project uses the RealSense ID SDK; see the SDK’s license terms. Code here is provided as a sample for use with the Intel RealSense ID SDK.
