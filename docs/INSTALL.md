# ZeroLantern 5D Mark II Installation & User Guide

This guide details the step-by-step procedure to prepare your CompactFlash (CF) card and install **ZeroLantern** (photographer-tailored Magic Lantern firmware) on a **Canon EOS 5D Mark II**.

---

## 1. Prerequisites

Before proceeding, verify that you have:
1. **Canon EOS 5D Mark II** running official **Canon Firmware 2.1.2**.
   > [!IMPORTANT]
   > If your camera runs an earlier firmware (e.g. 1.2.4 or 2.0.8), update to Canon official 2.1.2 first before installing ZeroLantern.
2. A **fully charged Canon LP-E6 battery** (never attempt firmware updates or bootflag flashing with a low battery).
3. A reliable **CompactFlash (CF) card** (16 GB, 32 GB, or 64 GB recommended; UDMA 7 capable).
4. A USB CF card reader connected to your computer.

---

## 2. Preparing the Bootable CF Card

DSLR cameras boot Magic Lantern / ZeroLantern from the memory card via an internal bootflag in camera ROM. The CF card itself must have its bootflag enabled.

### Option A: Windows (GUI via EOScard)
1. Insert the CF card into your card reader.
2. Format the card in camera first: Canon Menu $\rightarrow$ **Format** (select Low-level format if available).
3. Download and open [EOScard](https://pel.hu/eoscard/).
4. Select your CF card drive letter.
5. Ensure the **Magic Lantern** and **BOOTDISK** checkboxes are checked.
6. Click **Save** to write the FAT boot sector flags.

### Option B: macOS / Linux (Command Line)
1. Format the CF card with FAT32 or FAT16.
2. Run the bootflag injection script:
   ```bash
   # Linux/macOS
   chmod +x ./tools/make_bootable.sh
   ./tools/make_bootable.sh /dev/sdX1   # Replace with your actual CF card device
   ```

---

## 3. Copying ZeroLantern Files

1. Download or locate `release/ZeroLantern-5D2.212-StillPhoto.zip`.
2. Extract the archive contents directly to the **root** of your bootable CF card:
   ```text
   CF Card Root (e.g., E:\)
   ├── autoexec.bin           (ZeroLantern Core Binary)
   ├── ML-SETUP.FIR           (One-time Bootflag Installer)
   └── ML/
       ├── cropmks/           (Cinematic framing masks: XPan, CinemaScope)
       ├── data/              (Profile LUT tables)
       ├── fonts/             (High-readability LCD fonts)
       ├── modules/           (live_flt.mo, silent.mo, dual_iso.mo, ettr.mo, lua.mo)
       ├── scripts/           (Lua photography automations)
       └── README
   ```
3. Safely eject the CF card from your computer.

---

## 4. Enabling the Camera Bootflag (First-Time Only)

If your 5D Mark II has never run Magic Lantern before, enable the camera bootflag:
1. Insert the prepared CF card with `ML-SETUP.FIR` into the camera.
2. Power ON the camera and rotate the mode dial to **Manual (M)**.
3. Open Canon Menu $\rightarrow$ wrench tab 3 $\rightarrow$ **Firmware ver. 2.1.2**.
4. Press **SET** to start the firmware update process.
5. Follow the on-screen instructions. The screen will display green success text:
   ```text
   SUCCESS!
   Bootflags enabled.
   Restart your camera.
   ```
6. Power OFF the camera, remove and reinsert the battery, then close the battery and CF doors.

---

## 5. Booting ZeroLantern & Photographer Setup

1. Power ON the camera. The blue card activity LED will flash briefly as `autoexec.bin` loads.
2. Press the **Trash / Delete** button (or press `Q` if configured) to open the **ZeroLantern Menu**.
3. **Configure Recommended Photography Settings**:
   - **Look Tab**:
     - `Film Simulation`: Select your stock (e.g. *Kodak Portra 400*, *Cinestill 800T*, *Fuji Velvia 50*).
     - `Film Recipes`: Select a recipe (e.g. *Portra Warm*, *CineStill Night*, *Ilford HP5 Plus*, *Agfa Vista 200*).
     - `Fast-AF`: Set to **SET Button** for instantaneous predictive AF in LiveView.
     - `Digital Split-Image`: Set to **Classic Split** for manual focus lenses.
   - **Capture Tab**:
     - `Silent Picture`: Set to **ON** (Zero shutter-wear electronic shutter).
     - `Burst Governor`: Default is **5.0 FPS (Optimal)** for continuous burst without EDMAC overflow.
   - **Expo Tab**:
     - `Auto-ETTR`: Set to **Auto** for optimal highlight protection.

> [!TIP]
> **Back-Button AF Setup for Silent Shooting**:
> In Canon Custom Functions (`C.Fn IV -1: Shutter/AF-ON button`), assign AF to the back button (`*` or `AF-ON`). This frees the shutter half-press strictly for electronic sensor capture without triggering mechanical focusing hunting.

---

## 6. Safe Removal & Uninstallation

- **Temporary bypass**: Hold the **SET** button while turning the camera ON to boot the standard Canon stock firmware without loading ZeroLantern.
- **Permanent uninstallation**: Run the firmware update menu using `ML-SETUP.FIR` and wait for the screen prompt to remove the bootflag.
