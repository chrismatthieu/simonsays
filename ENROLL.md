# Enrolling Users on RealSense ID (F460)

Simon Says works best when users are **already enrolled** on the camera. New enrollment from Simon Says often fails on the **F460** because the device may require **secure (paired) mode** for enrollment, and Simon Says is built in non-secure mode.

Use one of these approaches to enroll users.

---

## If you see "Failed to recv sync bytes before timeout" or status 103

Your F46x is **paired** and only accepts **secure** sessions. The non-secure Simon Says build cannot start a session.

- **To use Simon Says (non-secure) again:** Unpair the device with the **same** secure app you used to pair (e.g. **Intel RealSense ID Viewer**). In the viewer: connect to the device, then click the **Unpair** (key) button. After unpairing, the device will accept non-secure sessions and Simon Says will connect.
- **To keep the device paired:** Use **Option 1** below: enroll in the viewer, then run Simon Says and answer **n** to enroll; Simon Says will authenticate using users you added in the viewer (the device stays paired).

---

## Option 1: Enroll with Intel RealSense ID Viewer (recommended)

Use the official **rsid-viewer** from the SDK to pair the device (once) and enroll users. Then run Simon Says and answer **n** to “Enroll a face?” — authentication will use the users you added in the viewer.

### Step 1: Build the SDK tools (viewer)

From a **new** build (separate from Simon Says), build the RealSense ID SDK with tools and secure mode:

1. Open a terminal (e.g. **Developer Command Prompt for VS 2022** or PowerShell with CMake and VS in PATH).

2. Configure and build the SDK **with tools and secure mode**:

   ```cmd
   cd C:\Users\cmatthie\Documents\SDK_2.7.3.0701_471615c_Standard
   mkdir build-tools
   cd build-tools
   cmake .. -G "Visual Studio 17 2022" -A x64 -DRSID_TOOLS=ON -DRSID_SECURE=1
   cmake --build . --config Release
   ```

   If CMake can’t find Visual Studio, use the same Ninja + vcvars approach you use for Simon Says, and add `-DRSID_TOOLS=ON -DRSID_SECURE=1`.

3. The viewer executable will be under something like:
   - `build-tools\bin\Release\rsid-viewer.exe`, or  
   - `build-tools\Release\rsid-viewer.exe`  
   (depending on the SDK layout). Run **rsid-viewer.exe** from that folder.

### Step 2: Pair and enroll in the viewer

1. Connect the F460 (COM4 or whatever port you use).
2. Run **rsid-viewer.exe**.
3. In the viewer, **pair the device** if it asks (first-time secure setup).
4. Use the viewer’s **Enroll** flow to add users (e.g. “player1” or any ID you like).

### Step 3: Use Simon Says

1. Close the viewer (so it doesn’t hold the COM port).
2. Run Simon Says: `run.cmd` from the Simon Says project root.
3. When prompted **Enroll a face? (y/n):** answer **n**.
4. Stand in front of the camera to **authenticate**; the stick man will run for enrolled users.

---

## Option 2: Unpair the device (so non-secure Simon Says works again)

If the F460 was **previously paired** with another app (e.g. the viewer), it may **only** accept secure sessions. In that case it can ignore non-secure session start and you get “Failed to recv sync bytes before timeout” and enrollment fails.

To **unpair** and return to non-secure mode:

1. Build the SDK **with secure mode and tools** (as in Option 1, Step 1).
2. **With rsid-viewer:** Run the viewer, connect to the F460 (same COM port). Use the **Unpair** button (key icon). After success, close the viewer and run Simon Says again. **With rsid-cli:** Run the CLI from the SDK build, connect, then press **i** for Unpair if it has an “Unpair” or “Reset pairing” option, and run it while the device is connected.
3. Or build and run the SDK’s **pair-device** sample with the **unpair** command (if your SDK version exposes that).

You must use the **same** secure app that was used when the device was paired. After unpairing, the device accepts non-secure sessions; Simon Says can then connect (device/firmware-dependent).

---

## Option 3: Build Simon Says with secure mode (in-app pairing + enrollment)

Simon Says can be built with **secure mode** so you can **pair** and **enroll** directly from the app (no viewer needed).

**Note:** In SDK 2.7.x, secure mode (RSID_SECURE) is **only supported for F45x**. If you have an **F46x/F460**, the app will exit with a message; use Option 1 (viewer) or build without secure (Option 1 + non-secure Simon Says) instead.

### Build with secure mode

From the Simon Says project root:

```cmd
set SIMONSAYS_SECURE=1
build.cmd
```

Or configure manually with CMake:

```cmd
cd build
cmake .. -DSIMONSAYS_SECURE=ON -DRSID_SDK_PATH=...  (and your usual generator/options)
```

This sets **RSID_SECURE=1** for the SDK, adds the **SignHelper** (ECDSA sign/verify) from `secure/`, and links **mbedtls**. The app then:

1. **On first run:** If no saved device key exists (`.rsid_device_key` in the current directory), it **pairs** with the device (host signs its public key, device returns its public key; the app saves the device key to `.rsid_device_key`).
2. **On later runs:** Loads the device key from `.rsid_device_key` and skips pairing.
3. **Enroll / Authenticate:** Same flow as non-secure; enrollment works on F460 because the session is secure.

### Run

```cmd
run.cmd
```

When prompted **Enroll a face? (y/n):** you can answer **y** to enroll a new user, or **n** to only authenticate. Pairing happens automatically before enroll/auth if needed.

---

## Summary

| Goal                         | What to do |
|-----------------------------|------------|
| **Enroll new users**        | Use **Option 1**: build SDK tools with `RSID_TOOLS=ON` and `RSID_SECURE=1`, run **rsid-viewer**, pair once, then enroll. |
| **Use Simon Says**          | Run Simon Says, answer **n** to enroll, then authenticate with users already enrolled in the viewer. |
| **Device only works paired**| Keep using the viewer (or another secure client) for enrollment; use Simon Says only for authenticate + stick man. |

If you tell me your exact SDK path and whether you prefer CMake or Visual Studio for the SDK build, I can give you a single copy-paste command block for Option 1 (build viewer + run + enroll).
