DDF Window Capture - Portable Edition
=====================================

Requirements
------------
- Windows 10 or Windows 11
- Desktop Window Manager (DWM), enabled by default
- The captured window must be in the same signed-in user's desktop session

No installation or administrator credentials are required. The application
runs with the permissions of the current user and does not write to the
registry, install a service, or copy files to Windows folders.

How to use
----------
1. Put Window-Capture.exe in any folder accessible to the user.
2. Open the Self KYC Form window.
3. Double-click Window-Capture.exe.
4. The first window whose title matches "*Self KYC Form*" opens in an
   always-on-top, resizable live preview.
5. If there is no matching window, select any open window from the graphical
   picker and click "Open preview".

Optional command-line switches
------------------------------
  Window-Capture.exe --picker
  Window-Capture.exe --title "*another window title*"
  Window-Capture.exe --width 640 --height 480

Troubleshooting
---------------
- Errors are shown on screen and logged to:
  %TEMP%\Window-Capture-error.log
- A Windows SmartScreen or company application-control warning is separate
  from administrator access. The executable is not digitally signed; your IT
  security policy may require signing or allow-listing before deployment.
- The app cannot capture a window belonging to another signed-in Windows
  session or another computer.

Security behavior
-----------------
- Requested execution level: asInvoker (never asks for elevation itself)
- No network access
- No installer
- No registry modification
- No persistent background process or service
