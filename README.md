# Window Refresher Utility

**Version:** 1.1
**Date:** 2025-05-07

## Overview

Window Refresher is a lightweight Windows console utility designed to automate the task of sending `Ctrl+F5` (hard refresh) keystrokes to a specific application window at random intervals. This can be useful for automatically refreshing web pages, monitoring applications that update their content, or other scenarios where periodic hard refreshes are needed without manual intervention.

The program allows you to:
1.  **Select a Target Window:** Simply click on any part of the window you wish to target.
2.  **Visual Confirmation:** The selected window will briefly flash to confirm your choice.
3.  **Configurable Random Delays:** Set minimum and maximum delay times (in seconds) in a simple `options.config` file. The program will wait a random duration within this range before sending each keystroke.
4.  **Background Operation:** Once a window is selected, the program will attempt to send keystrokes to it even if it's not the currently active (foreground) window.
5.  **User-Friendly Console Output:** Clear messages guide you through the process and provide status updates.
6.  **Detailed Debug Logging:** A `debug.log` file records detailed operational information for troubleshooting.

## Features

*   Interactive window selection via mouse click.
*   Visual feedback (window flash) upon selection.
*   Randomized delay intervals for sending keystrokes.
*   Configuration file (`options.config`) for easy customization of delay times.
*   Attempts to send keystrokes to the target window even if it's in the background.
    *   *Note: This involves briefly bringing the target window to the foreground to ensure reliable input delivery.*
*   Graceful handling of scenarios where the target window is closed or system UI (like Alt+Tab) is active.
*   Detailed logging for diagnostics.

## Prerequisites

*   **Operating System:** Windows 7 or newer.
*   No external dependencies are required to run the compiled executable.

## How to Use

1.  **Download/Compile:**
    *   If you have the compiled `window_refresher.exe`, place it in a folder of your choice.
    *   If you are compiling from source (see [Compilation](#compilation) section below), the executable will be generated.

2.  **Configure Delays (Optional but Recommended):**
    *   In the same folder as `window_refresher.exe`, create a text file named `options.config`.
    *   Add the following lines to set your desired minimum and maximum delay times (in seconds). You can use decimal values.
      ```ini
      # Example options.config
      # Lines starting with # or ; are comments

      min_delay = 5.0  # Minimum 5 seconds
      max_delay = 15.5 # Maximum 15.5 seconds
      ```
    *   If `options.config` is not found, or if the values are invalid, the program will use default delays (Min: 2.0s, Max: 7.0s) and will attempt to create a default `options.config` file for you.

3.  **Run the Program:**
    *   Open a Command Prompt or PowerShell window.
    *   Navigate to the folder where `window_refresher.exe` is located.
    *   Run the executable by typing: `.\window_refresher.exe` and pressing Enter.

4.  **Select the Target Window:**
    *   The console will display:
      ```
      --- Window Selection ---
      Please CLICK ANYWHERE on the window you want to target.
      Waiting for your click...
      ```
    *   Switch to the application window you want to send `Ctrl+F5` to.
    *   **Left-click once** anywhere within that window.

5.  **Confirmation:**
    *   The console will show the title of the window you selected.
    *   The selected window itself will briefly flash its border/title bar a few times as a visual confirmation.

6.  **Operation:**
    *   The program will now enter its main loop.
    *   It will display messages in the console indicating how long it's waiting and when it's sending the `Ctrl+F5` keystroke.
    *   Example console output:
      ```
      Target window acquired. Flashing for confirmation...

      Starting random Ctrl+F5 keystrokes to the selected window.
      Delays will be between 5.0s and 15.5s.
      Press Ctrl+C in this console to stop the program.
      Waiting for 10.32s before sending Ctrl+F5 to "Example Web Page - Browser"...
      Sending Ctrl+F5 (Count: 1) to window "Example Web Page - Browser"...
      Waiting for 7.89s before sending Ctrl+F5 to "Example Web Page - Browser"...
      Sending Ctrl+F5 (Count: 2) to window "Example Web Page - Browser"...
      ```

7.  **Stopping the Program:**
    *   To stop the program, switch back to the console window where `window_refresher.exe` is running and press `Ctrl+C`.

## Important Notes

*   **Focus Handling:** To reliably send keystrokes (especially complex ones like `Ctrl+F5`) to a window that might not be active, this program will briefly attempt to bring the target window to the foreground, send the input, and then restore the previously active window. This may cause a quick visual "flash" of the target window.
*   **Alt+Tab and System UI:** If you are actively using Alt+Tab or other system-level UI (like the Start Menu or a UAC prompt) when the program attempts to send a keystroke, it will detect that the `Alt` key is pressed or that it cannot reliably switch focus. In such cases, it will skip sending the keystroke for that cycle and try again after the next random delay. This is to prevent interference with your actions.
*   **Target Application Compatibility:** While this method is generally robust, some highly specialized applications or games with custom input handling might not respond as expected.
*   **Administrator Privileges:** Running this program does not typically require administrator privileges. However, if the target window is an application running with elevated (administrator) privileges, `window_refresher.exe` might also need to be run with administrator privileges to interact with it successfully.

## Troubleshooting

*   **Keystrokes Not Registering:**
    1.  Ensure you have selected the correct top-level window of the application.
    2.  Check if the target application is running with administrator privileges. If so, try running `window_refresher.exe` as an administrator.
    3.  Look at the `debug.log` file created in the same directory as the executable. It contains detailed information about the program's operations, including focus switching attempts and errors.
*   **`options.config` Not Working:**
    1.  Make sure the file is named exactly `options.config` and is in the same folder as `window_refresher.exe`.
    2.  Ensure the format is `key = value` (e.g., `min_delay = 3.0`).
    3.  Check `debug.log` for messages about loading the configuration.
*   **Program Exits Unexpectedly:** Check `debug.log` for any error messages.

## Compilation (from Source)

If you have the source code (`window_refresher.c` or `main.c`) and want to compile it yourself:

1.  **Compiler:** You'll need a C compiler that supports C99 and the Windows API. MinGW-w64 (via MSYS2) is recommended for Windows.
2.  **MSYS2 UCRT64 Environment (Recommended):**
    *   Open the MSYS2 UCRT64 terminal.
    *   Navigate to the directory containing the source code.
    *   Compile using GCC:
      ```bash
      gcc window_refresher.c -o window_refresher.exe -lgdi32 -luser32 -ladvapi32 -Wall -Wextra -O2
      ```
      *   `-lgdi32`, `-luser32`, `-ladvapi32`: Link against necessary Windows libraries.
      *   `-Wall -Wextra`: Enable common and extra compiler warnings (good practice).
      *   `-O2`: Optimization level (optional).

## Future Enhancements (Ideas)

*   Option to specify keystroke combination in `options.config`.
*   System tray icon and background operation without a visible console.
*   More sophisticated focus detection using `GetGUIThreadInfo`.
*   Option to select window by title/class name from a list.

## License

This software is provided "as-is", without any express or implied warranty. In no event shall the authors be held liable for any damages arising from the use of this software.
