/**
 * @file window_refresher.c
 * @brief A C program to repeatedly send Ctrl+F5 keystrokes to a user-selected window
 *        at random intervals defined in a configuration file.
 *
 * This program allows a user to click on a target window. It then periodically
 * sends a Ctrl+F5 keystroke combination to that window, even if it's not
 * the active foreground window. The delay between keystrokes is randomized
 * between a minimum and maximum value, configurable via "options.config".
 *
 * Compilation (MinGW GCC):
 * gcc window_refresher.c -o window_refresher.exe -lgdi32 -luser32 -ladvapi32 -Wall -Wextra -pedantic -O2
 *
 * @version 1.1
 * @date 2025-05-07
 */

// Target Windows 7 or newer for API compatibility (e.g., GetAncestor, CryptGenRandom)
#define _WIN32_WINNT 0x0601

// Disable specific MSVC warnings if ever compiled with it, not strictly needed for GCC
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <limits.h> // For UINT_MAX
#include <ctype.h>  // For isspace
#include <windows.h>
#include <wincrypt.h> // For CryptGenRandom

// === Constants ===
#define MAX_TITLE_LENGTH 256
#define MAX_CONFIG_LINE_LENGTH 256
#define MAX_CONFIG_KEY_LENGTH 128
#define MAX_CONFIG_VALUE_LENGTH 128
#define DEFAULT_MIN_DELAY_S 2.0
#define DEFAULT_MAX_DELAY_S 7.0
#define ALT_KEY_CHECK_DELAY_MS 500
#define FOCUS_SWITCH_ATTEMPTS 3
#define FOCUS_SWITCH_RETRY_DELAY_MS 100
#define FOCUS_SETTLE_DELAY_MS 350
#define POST_SENDINPUT_DELAY_MS 100
#define MAIN_LOOP_POLL_INTERVAL_MS 50 // For GetAsyncKeyState in SelectWindowByClick

const char* CONFIG_FILE_NAME = "options.config";
const char* DEBUG_LOG_FILE_NAME = "debug.log";

// === Global Variables ===
// These are global for convenience in this single-file application.
// In a larger project, they might be part of a context struct.

/** @brief File pointer for the debug log. */
static FILE* g_debug_log_file = NULL;

/** @brief Handle to the window targeted for keystrokes. */
static HWND  g_hTargetWindow = NULL;

/** @brief Minimum delay between keystrokes, in seconds. Loaded from config. */
static double g_min_delay_seconds = DEFAULT_MIN_DELAY_S;

/** @brief Maximum delay between keystrokes, in seconds. Loaded from config. */
static double g_max_delay_seconds = DEFAULT_MAX_DELAY_S;


// === Function Prototypes ===
// Logging
static void LogMessage(const char *level, const char *format, va_list args);
static void LogDebug(const char *format, ...);
static void LogInfo(const char *format, ...); // For user-facing info that also goes to log
static void LogWarning(const char *format, ...);
static void LogError(const char *format, ...);
static BOOL InitializeLogging(void);
static void ShutdownLogging(void);

// Configuration
static char* TrimWhitespace(char *str);
static BOOL CreateDefaultConfigFile(void);
static void LoadConfiguration(void);

// Window Interaction
static void FlashTargetWindow(HWND hWnd);
static HWND GetTopLevelWindowFromClick(void);
static BOOL ActivateWindowAndEnsureFocus(HWND hWndToActivate, HWND hOriginalForeground);
static void RestoreOriginalFocus(HWND hOriginalForeground, HWND hTargetWindow, BOOL focusSwitchedSuccessfully);
static void SendCtrlF5Keystroke(HWND targetHwnd);

// Utilities
static double GetRandomDelaySeconds(double min_s, double max_s);
static void WaitMilliseconds(DWORD milliseconds);
static BOOL IsAltKeyHeld(void);

// === Main Application Logic ===

/**
 * @brief Main entry point of the application.
 * Initializes logging and configuration, selects a target window,
 * and enters a loop to send keystrokes.
 * @return EXIT_SUCCESS on normal termination, EXIT_FAILURE on error.
 */
int main(void) {
    if (!InitializeLogging()) {
        // If logging init fails, printf is the fallback for critical errors
        printf("CRITICAL: Failed to initialize logging. Exiting.\n");
        return EXIT_FAILURE;
    }

    LogInfo("Program started. Mode: Targeted Window Keystroke Sender with Config.");
    printf("Welcome! This program will send Ctrl+F5 to a window you select at random intervals.\n");

    LoadConfiguration();

    // Seed random number generator
    LARGE_INTEGER perfCounter;
    if (QueryPerformanceCounter(&perfCounter)) {
        srand((unsigned int)time(NULL) ^ (unsigned int)perfCounter.QuadPart ^ (unsigned int)GetCurrentProcessId());
    } else {
        srand((unsigned int)time(NULL) ^ (unsigned int)GetCurrentProcessId()); // Fallback seeding
    }

    g_hTargetWindow = GetTopLevelWindowFromClick();

    if (g_hTargetWindow == NULL) {
        printf("No window was selected. Exiting program.\n");
        LogError("Main: No target window selected. Program will exit.");
        ShutdownLogging();
        return EXIT_FAILURE;
    }

    printf("Target window acquired. Flashing for confirmation...\n");
    FlashTargetWindow(g_hTargetWindow);
    WaitMilliseconds(1000); // Give user a moment

    printf("\nStarting random Ctrl+F5 keystrokes to the selected window.\n");
    printf("Delays will be between %.1fs and %.1fs.\n", g_min_delay_seconds, g_max_delay_seconds);
    printf("Press Ctrl+C in this console to stop the program.\n");
    LogInfo("Main: Entering main loop to send keystrokes to HWND %p. MinDelay: %.2f, MaxDelay: %.2f",
             (void*)g_hTargetWindow, g_min_delay_seconds, g_max_delay_seconds);

    int keystroke_count = 0;
    while (TRUE) { // Loop indefinitely until Ctrl+C or error
        if (!IsWindow(g_hTargetWindow)) {
            printf("Target window (HWND %p) no longer exists. Stopping.\n", (void*)g_hTargetWindow);
            LogWarning("Main: Target window HWND %p no longer exists. Exiting loop.", (void*)g_hTargetWindow);
            break;
        }

        double wait_duration_s = GetRandomDelaySeconds(g_min_delay_seconds, g_max_delay_seconds);
        char windowTitle[MAX_TITLE_LENGTH] = "N/A";
        GetWindowText(g_hTargetWindow, windowTitle, MAX_TITLE_LENGTH);

        printf("Waiting for %.2fs before sending Ctrl+F5 to \"%s\"...\n", wait_duration_s, (strlen(windowTitle) > 0 ? windowTitle : "No Title"));
        LogDebug("Main: Waiting for %.3f seconds.", wait_duration_s);
        WaitMilliseconds((DWORD)(wait_duration_s * 1000.0));

        if (IsAltKeyHeld()) {
            printf("Info: Alt key is currently pressed. Skipping keystroke to avoid conflict.\n");
            LogDebug("Main: Alt key detected as pressed. Deferring SendCtrlF5Keystroke.");
            WaitMilliseconds(ALT_KEY_CHECK_DELAY_MS);
            continue;
        }

        if (!IsWindow(g_hTargetWindow)) { // Re-check after wait and Alt key check
            printf("Target window (HWND %p) disappeared before sending keystroke. Stopping.\n", (void*)g_hTargetWindow);
            LogWarning("Main: Target window HWND %p disappeared during wait. Exiting loop.", (void*)g_hTargetWindow);
            break;
        }

        keystroke_count++;
        printf("Sending Ctrl+F5 (Count: %d) to window \"%s\"...\n", keystroke_count, (strlen(windowTitle) > 0 ? windowTitle : "No Title"));
        SendCtrlF5Keystroke(g_hTargetWindow);

        WaitMilliseconds(POST_SENDINPUT_DELAY_MS);
    }

    printf("Program loop terminated.\n");
    LogInfo("Program finished.");
    ShutdownLogging();
    return EXIT_SUCCESS;
}

// === Logging Functions ===

/**
 * @brief Core logging function.
 * Prepends a timestamp and level to the message and writes to the debug log file.
 * @param level Log level string (e.g., "DEBUG", "INFO").
 * @param format Format string for the message.
 * @param args Variable arguments list.
 */
static void LogMessage(const char *level, const char *format, va_list args) {
    if (g_debug_log_file == NULL) return;

    time_t now;
    time(&now);
    char time_buf[30];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(g_debug_log_file, "[%s] [%s] ", time_buf, level);
    vfprintf(g_debug_log_file, format, args);
    fprintf(g_debug_log_file, "\n"); // Ensure newline
    fflush(g_debug_log_file);
}

/** @brief Logs a debug message. */
static void LogDebug(const char *format, ...) {
    va_list args;
    va_start(args, format);
    LogMessage("DEBUG", format, args);
    va_end(args);
}

/** @brief Logs an informational message. */
static void LogInfo(const char *format, ...) {
    va_list args;
    va_start(args, format);
    LogMessage("INFO", format, args);
    va_end(args);
}

/** @brief Logs a warning message. */
static void LogWarning(const char *format, ...) {
    va_list args;
    va_start(args, format);
    LogMessage("WARNING", format, args);
    va_end(args);
}

/** @brief Logs an error message. */
static void LogError(const char *format, ...) {
    va_list args;
    va_start(args, format);
    LogMessage("ERROR", format, args);
    va_end(args);
}


/**
 * @brief Initializes the debug logging system.
 * Opens the debug log file for writing.
 * @return TRUE if initialization was successful, FALSE otherwise.
 */
static BOOL InitializeLogging(void) {
    g_debug_log_file = fopen(DEBUG_LOG_FILE_NAME, "w");
    if (g_debug_log_file == NULL) {
        perror("CRITICAL ERROR opening debug.log"); // Use perror for system errors
        return FALSE;
    }
    // Log successful initialization to the file itself
    LogInfo("Logging system initialized.");
    return TRUE;
}

/**
 * @brief Shuts down the logging system.
 * Closes the debug log file.
 */
static void ShutdownLogging(void) {
    if (g_debug_log_file != NULL) {
        LogInfo("Logging system shutting down.");
        fclose(g_debug_log_file);
        g_debug_log_file = NULL;
    }
}

// === Configuration Functions ===

/**
 * @brief Trims leading and trailing whitespace from a string in-place.
 * @param str The string to trim.
 * @return Pointer to the beginning of the trimmed string (which is str itself or a part of it).
 */
static char* TrimWhitespace(char *str) {
    char *end;
    // Trim leading space
    while(isspace((unsigned char)*str)) str++;
    if(*str == '\0') return str; // All spaces?
    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    // Write new null terminator
    *(end + 1) = '\0';
    return str;
}

/**
 * @brief Creates a default configuration file if one doesn't exist.
 * @return TRUE if the file was created or already existed, FALSE on creation error.
 */
static BOOL CreateDefaultConfigFile(void) {
    FILE* configFile = fopen(CONFIG_FILE_NAME, "w");
    if (configFile == NULL) {
        printf("Warning: Could not create default '%s'.\n", CONFIG_FILE_NAME);
        LogWarning("LoadConfig: Failed to create default '%s'.", CONFIG_FILE_NAME);
        return FALSE;
    }
    fprintf(configFile, "# Configuration for Window Refresher\n");
    fprintf(configFile, "# Delays are in seconds (can be fractional, e.g., 2.5)\n");
    fprintf(configFile, "min_delay = %.1f\n", DEFAULT_MIN_DELAY_S);
    fprintf(configFile, "max_delay = %.1f\n", DEFAULT_MAX_DELAY_S);
    fclose(configFile);
    printf("Info: A default '%s' has been created.\n", CONFIG_FILE_NAME);
    LogInfo("LoadConfig: Created default '%s'.", CONFIG_FILE_NAME);
    return TRUE;
}

/**
 * @brief Loads configuration settings from "options.config".
 * Reads min_delay and max_delay. If the file doesn't exist,
 * it uses default values and attempts to create a default config file.
 */
static void LoadConfiguration(void) {
    FILE *configFile = fopen(CONFIG_FILE_NAME, "r");
    char line[MAX_CONFIG_LINE_LENGTH];
    char key[MAX_CONFIG_KEY_LENGTH];
    char value_str[MAX_CONFIG_VALUE_LENGTH];

    // Set defaults initially
    g_min_delay_seconds = DEFAULT_MIN_DELAY_S;
    g_max_delay_seconds = DEFAULT_MAX_DELAY_S;

    if (configFile == NULL) {
        printf("Info: '%s' not found. Using default delay values (Min: %.1fs, Max: %.1fs).\n",
               CONFIG_FILE_NAME, g_min_delay_seconds, g_max_delay_seconds);
        LogInfo("LoadConfig: '%s' not found. Using default delays.", CONFIG_FILE_NAME);
        CreateDefaultConfigFile(); // Attempt to create it
        return;
    }

    LogInfo("LoadConfig: Reading configuration from '%s'.", CONFIG_FILE_NAME);
    int line_num = 0;
    while (fgets(line, sizeof(line), configFile) != NULL) {
        line_num++;
        char *trimmed_line = TrimWhitespace(line);

        if (trimmed_line[0] == '\0' || trimmed_line[0] == '#' || trimmed_line[0] == ';') {
            continue; // Skip empty lines and comments
        }

        if (sscanf(trimmed_line, "%127[^= \t] = %127s", key, value_str) == 2) {
            char *trimmed_key = TrimWhitespace(key);
            char *trimmed_value_str = TrimWhitespace(value_str);
            double parsed_val = atof(trimmed_value_str);

            if (strcmp(trimmed_key, "min_delay") == 0) {
                if (parsed_val > 0.0 && parsed_val < 3600.0) { // Basic validation
                    g_min_delay_seconds = parsed_val;
                    LogDebug("LoadConfig: Loaded min_delay = %.2f", g_min_delay_seconds);
                } else {
                    LogWarning("LoadConfig: Invalid value for min_delay on line %d: '%s'. Using default or previous.", line_num, trimmed_value_str);
                }
            } else if (strcmp(trimmed_key, "max_delay") == 0) {
                 if (parsed_val > 0.0 && parsed_val < 3600.0) { // Basic validation
                    g_max_delay_seconds = parsed_val;
                    LogDebug("LoadConfig: Loaded max_delay = %.2f", g_max_delay_seconds);
                } else {
                    LogWarning("LoadConfig: Invalid value for max_delay on line %d: '%s'. Using default or previous.", line_num, trimmed_value_str);
                }
            } else {
                LogWarning("LoadConfig: Unknown key '%s' on line %d.", trimmed_key, line_num);
            }
        } else {
            LogWarning("LoadConfig: Could not parse line %d: '%s'", line_num, trimmed_line);
        }
    }
    fclose(configFile);

    if (g_min_delay_seconds > g_max_delay_seconds) {
        printf("Warning: min_delay (%.1fs) in config is greater than max_delay (%.1fs). Swapping them.\n",
               g_min_delay_seconds, g_max_delay_seconds);
        LogWarning("LoadConfig: min_delay > max_delay. Swapping. Min: %.2f, Max: %.2f", g_min_delay_seconds, g_max_delay_seconds);
        double temp = g_min_delay_seconds;
        g_min_delay_seconds = g_max_delay_seconds;
        g_max_delay_seconds = temp;
    }
    printf("Info: Using delays - Min: %.1fs, Max: %.1fs (from '%s').\n",
           g_min_delay_seconds, g_max_delay_seconds, CONFIG_FILE_NAME);
}


// === Window Interaction Functions ===

/**
 * @brief Briefly flashes the border/title bar of the specified window.
 * @param hWnd Handle to the window to flash.
 */
static void FlashTargetWindow(HWND hWnd) {
    if (!IsWindow(hWnd)) {
        LogWarning("FlashTargetWindow: Invalid window handle provided (%p).", (void*)hWnd);
        return;
    }
    FLASHWINFO fwi;
    fwi.cbSize = sizeof(FLASHWINFO);
    fwi.hwnd = hWnd;
    fwi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG; // Flash caption and taskbar, don't steal focus if not foreground
    fwi.uCount = 3;    // Number of flashes
    fwi.dwTimeout = 0; // Default flash rate (system default)
    if (!FlashWindowEx(&fwi)) {
        LogError("FlashTargetWindow: FlashWindowEx failed for HWND %p. Error: %lu", (void*)hWnd, GetLastError());
    } else {
        LogDebug("FlashTargetWindow: Flashed window HWND: %p", (void*)hWnd);
    }
}

/**
 * @brief Prompts the user to click on a window and returns its top-level handle.
 * @return HWND of the selected top-level window, or NULL on failure or if no window is found.
 */
static HWND GetTopLevelWindowFromClick(void) {
    POINT cursorPos;
    HWND clickedHwnd = NULL;
    HWND topLevelHwnd = NULL;

    printf("\n--- Window Selection ---\n");
    printf("Please CLICK ANYWHERE on the window you want to target.\n");
    printf("Waiting for your click...\n");
    fflush(stdout);
    LogDebug("GetTopLevelWindowFromClick: Waiting for left mouse button click.");

    // Wait for left mouse button press
    while (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        WaitMilliseconds(MAIN_LOOP_POLL_INTERVAL_MS);
    }
    LogDebug("GetTopLevelWindowFromClick: Left mouse button pressed.");

    // Wait for the button to be released to avoid issues with drag/multiple clicks
    while (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
        WaitMilliseconds(MAIN_LOOP_POLL_INTERVAL_MS);
    }
    LogDebug("GetTopLevelWindowFromClick: Left mouse button released.");

    if (GetCursorPos(&cursorPos)) {
        clickedHwnd = WindowFromPoint(cursorPos);
        if (clickedHwnd != NULL) {
            // Try to get the ultimate owner window (the main application window)
            topLevelHwnd = GetAncestor(clickedHwnd, GA_ROOTOWNER);
            if (topLevelHwnd == NULL) { // Fallback to GA_ROOT if GA_ROOTOWNER fails
                topLevelHwnd = GetAncestor(clickedHwnd, GA_ROOT);
            }
            if (topLevelHwnd == NULL) { // If still NULL, use the directly clicked window
                topLevelHwnd = clickedHwnd;
            }

            char windowTitle[MAX_TITLE_LENGTH];
            GetWindowText(topLevelHwnd, windowTitle, MAX_TITLE_LENGTH);
            printf("Window selected: \"%s\" (HWND: %p)\n",
                   (strlen(windowTitle) > 0 ? windowTitle : "No Title"), (void*)topLevelHwnd);
            LogInfo("GetTopLevelWindowFromClick: Click at (%ld, %ld). WindowFromPoint HWND: %p. Top-level HWND: %p. Title: %s",
                         cursorPos.x, cursorPos.y, (void*)clickedHwnd, (void*)topLevelHwnd, windowTitle);
            return topLevelHwnd;
        } else {
            printf("Could not identify a window at the click position.\n");
            LogWarning("GetTopLevelWindowFromClick: WindowFromPoint returned NULL.");
        }
    } else {
        printf("Failed to get cursor position.\n");
        LogError("GetTopLevelWindowFromClick: GetCursorPos failed. Error: %lu", GetLastError());
    }
    return NULL;
}


/**
 * @brief Attempts to activate the target window and ensure it has focus.
 * This involves attaching thread inputs and using SetForegroundWindow.
 * @param hWndToActivate The window to activate.
 * @param hOriginalForeground The window that was originally in the foreground.
 * @return TRUE if focus was successfully switched (or target was already foreground), FALSE otherwise.
 */
static BOOL ActivateWindowAndEnsureFocus(HWND hWndToActivate, HWND hOriginalForeground) {
    if (hOriginalForeground == hWndToActivate) {
        LogDebug("ActivateWindow: Target window %p is already foreground.", (void*)hWndToActivate);
        return TRUE; // Already foreground
    }

    LogDebug("ActivateWindow: Target %p is not foreground. Attempting to activate.", (void*)hWndToActivate);

    DWORD dwCurrentThreadId = GetCurrentThreadId();
    DWORD dwTargetThreadId = GetWindowThreadProcessId(hWndToActivate, NULL);
    DWORD dwOriginalForegroundThreadId = GetWindowThreadProcessId(hOriginalForeground, NULL);

    BOOL attachedToTarget = FALSE;
    BOOL attachedToOriginalFG = FALSE;

    if (dwTargetThreadId != 0 && dwTargetThreadId != dwCurrentThreadId) {
        if (AttachThreadInput(dwCurrentThreadId, dwTargetThreadId, TRUE)) {
            attachedToTarget = TRUE;
        } else {
            LogWarning("ActivateWindow: Failed to attach to target thread %lu. Error: %lu", dwTargetThreadId, GetLastError());
        }
    }
    if (hOriginalForeground && dwOriginalForegroundThreadId != 0 &&
        dwOriginalForegroundThreadId != dwCurrentThreadId &&
        dwOriginalForegroundThreadId != dwTargetThreadId) { // Avoid double attach
        if (AttachThreadInput(dwCurrentThreadId, dwOriginalForegroundThreadId, TRUE)) {
            attachedToOriginalFG = TRUE;
        } else {
            LogWarning("ActivateWindow: Failed to attach to original FG thread %lu. Error: %lu", dwOriginalForegroundThreadId, GetLastError());
        }
    }

    if (IsIconic(hWndToActivate)) {
        LogDebug("ActivateWindow: Target %p is iconic, restoring.", (void*)hWndToActivate);
        ShowWindow(hWndToActivate, SW_RESTORE);
        WaitMilliseconds(FOCUS_SWITCH_RETRY_DELAY_MS);
    }

    BOOL focusSet = FALSE;
    for (int i = 0; i < FOCUS_SWITCH_ATTEMPTS; ++i) {
        SetForegroundWindow(hWndToActivate);
        WaitMilliseconds(FOCUS_SWITCH_RETRY_DELAY_MS);
        if (GetForegroundWindow() == hWndToActivate) {
            focusSet = TRUE;
            LogDebug("ActivateWindow: SetForegroundWindow for %p succeeded on attempt %d.", (void*)hWndToActivate, i + 1);
            break;
        }
        LogDebug("ActivateWindow: SetForegroundWindow for %p failed on attempt %d. Current FG: %p",
                 (void*)hWndToActivate, i + 1, (void*)GetForegroundWindow());
    }
    
    if (focusSet) {
        LogDebug("ActivateWindow: Pausing (%dms) for system to settle.", FOCUS_SETTLE_DELAY_MS);
        WaitMilliseconds(FOCUS_SETTLE_DELAY_MS);
        if (GetForegroundWindow() != hWndToActivate) {
            LogWarning("ActivateWindow: Focus lost from target %p after settling pause. Current FG: %p.",
                       (void*)hWndToActivate, (void*)GetForegroundWindow());
            focusSet = FALSE; // Mark as failed if focus was lost
        } else {
            LogDebug("ActivateWindow: Target %p still has focus after settling pause.", (void*)hWndToActivate);
        }
    } else {
         LogWarning("ActivateWindow: Failed to set foreground to target %p after %d attempts.", (void*)hWndToActivate, FOCUS_SWITCH_ATTEMPTS);
    }

    // Detach in reverse order of attach, and only if attached
    if (attachedToOriginalFG) AttachThreadInput(dwCurrentThreadId, dwOriginalForegroundThreadId, FALSE);
    if (attachedToTarget) AttachThreadInput(dwCurrentThreadId, dwTargetThreadId, FALSE);
    
    return focusSet;
}

/**
 * @brief Restores focus to the original foreground window if conditions are met.
 * @param hOriginalForeground The window that was originally in the foreground.
 * @param hTargetWindow The window that was targeted for input.
 * @param focusSwitchedSuccessfully Whether the focus was successfully switched to the target.
 */
static void RestoreOriginalFocus(HWND hOriginalForeground, HWND hTargetWindow, BOOL focusSwitchedSuccessfully) {
    if (hOriginalForeground == hTargetWindow || !hOriginalForeground || !IsWindow(hOriginalForeground)) {
        return; // No need or nothing to restore to
    }

    if (!focusSwitchedSuccessfully) {
        LogDebug("RestoreFocus: Input was not sent to target, not aggressively restoring original focus.");
        return;
    }
    
    HWND currentFgAfterInput = GetForegroundWindow();
    // Only restore if the target is still foreground, or if user switched to something else (not original)
    if (currentFgAfterInput == hTargetWindow || currentFgAfterInput != hOriginalForeground) {
        LogDebug("RestoreFocus: Attempting to restore original foreground to HWND %p", (void*)hOriginalForeground);
        WaitMilliseconds(FOCUS_SWITCH_RETRY_DELAY_MS); // Brief pause

        DWORD dwCurrentThreadId = GetCurrentThreadId();
        DWORD dwOriginalFGThreadId = GetWindowThreadProcessId(hOriginalForeground, NULL);
        BOOL needsAttach = (dwOriginalFGThreadId != 0 && dwOriginalFGThreadId != dwCurrentThreadId);

        if (needsAttach) AttachThreadInput(dwCurrentThreadId, dwOriginalFGThreadId, TRUE);
        
        if (IsIconic(hOriginalForeground)) ShowWindow(hOriginalForeground, SW_RESTORE);
        SetForegroundWindow(hOriginalForeground); // Attempt to restore
        
        if (needsAttach) AttachThreadInput(dwCurrentThreadId, dwOriginalFGThreadId, FALSE);

        if (GetForegroundWindow() == hOriginalForeground) {
            LogDebug("RestoreFocus: Successfully restored foreground to HWND %p", (void*)hOriginalForeground);
        } else {
            LogWarning("RestoreFocus: Failed to restore foreground to HWND %p. Current FG: %p",
                       (void*)hOriginalForeground, (void*)GetForegroundWindow());
        }
    } else {
        LogDebug("RestoreFocus: Original foreground window %p is already active or user switched. No restore needed.", (void*)hOriginalForeground);
    }
}


/**
 * @brief Sends a Ctrl+F5 keystroke combination to the target window.
 * This function attempts to bring the target window to the foreground briefly
 * to ensure reliable keystroke delivery via SendInput.
 * @param targetHwnd Handle to the window to receive the keystrokes.
 */
static void SendCtrlF5Keystroke(HWND targetHwnd) {
    if (!IsWindow(targetHwnd)) {
        LogWarning("SendCtrlF5: Target HWND %p is invalid. Skipping.", (void*)targetHwnd);
        printf("Warning: The target window seems to be closed. Keystroke not sent.\n");
        return;
    }

    HWND hOriginalForeground = GetForegroundWindow();
    BOOL targetWasAlreadyForeground = (hOriginalForeground == targetHwnd);
    BOOL focusSetForInput = targetWasAlreadyForeground;

    if (!targetWasAlreadyForeground) {
        focusSetForInput = ActivateWindowAndEnsureFocus(targetHwnd, hOriginalForeground);
    } else {
        LogDebug("SendCtrlF5: Target window %p is already foreground.", (void*)targetHwnd);
    }

    if (focusSetForInput) {
        // Final check: ensure window is not iconic just before sending
        if (IsIconic(targetHwnd)) {
            LogDebug("SendCtrlF5: Target %p became iconic before SendInput. Restoring.", (void*)targetHwnd);
            ShowWindow(targetHwnd, SW_RESTORE);
            WaitMilliseconds(FOCUS_SWITCH_RETRY_DELAY_MS);
            if (GetForegroundWindow() != targetHwnd) {
                LogWarning("SendCtrlF5: Failed to keep target %p foreground after restore. Skipping SendInput.", (void*)targetHwnd);
                focusSetForInput = FALSE; // Mark as failed
            }
        }

        if (focusSetForInput) {
            INPUT inputs[4] = {0};
            inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
            inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = VK_F5;
            inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = VK_F5;      inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

            UINT uSent = SendInput(4, inputs, sizeof(INPUT));
            if (uSent != 4) {
                LogError("SendCtrlF5 (SendInput): Failed. Sent %u of 4. Error: %lu", uSent, GetLastError());
            } else {
                LogDebug("SendCtrlF5 (SendInput): Sent Ctrl+F5 to HWND %p.", (void*)targetHwnd);
            }
        }
    } else {
        printf("Info: Could not reliably switch to target window. Keystroke for Ctrl+F5 skipped this cycle.\n");
        // Logged sufficiently by ActivateWindowAndEnsureFocus
    }

    // Restore original focus if necessary
    if (!targetWasAlreadyForeground) {
         RestoreOriginalFocus(hOriginalForeground, targetHwnd, focusSetForInput);
    }
}


// === Utility Functions ===

/**
 * @brief Generates a random delay in seconds between a specified min and max.
 * Uses CryptGenRandom for better randomness if available, falls back to rand().
 * @param min_s Minimum delay in seconds.
 * @param max_s Maximum delay in seconds.
 * @return Random delay in seconds.
 */
static double GetRandomDelaySeconds(double min_s, double max_s) {
    if (min_s >= max_s) {
        LogDebug("GetRandomDelay: min_s (%.2f) >= max_s (%.2f). Returning min_s.", min_s, max_s);
        return min_s;
    }

    HCRYPTPROV hCryptProv = 0;
    unsigned int random_value;
    BOOL cryptoSuccess = FALSE;

    if (CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        if (CryptGenRandom(hCryptProv, sizeof(random_value), (BYTE*)&random_value)) {
            cryptoSuccess = TRUE;
        } else {
            LogError("GetRandomDelay: CryptGenRandom failed. Error: %lu. Falling back to rand().", GetLastError());
        }
        CryptReleaseContext(hCryptProv, 0);
    } else {
        LogError("GetRandomDelay: CryptAcquireContext failed. Error: %lu. Falling back to rand().", GetLastError());
    }

    if (!cryptoSuccess) {
        random_value = rand(); // Fallback
    }

    double scale = (double)random_value / (double)UINT_MAX;
    return min_s + scale * (max_s - min_s);
}

/**
 * @brief Pauses execution for a specified number of milliseconds.
 * @param milliseconds Duration to wait.
 */
static void WaitMilliseconds(DWORD milliseconds) {
    if (milliseconds > 0) {
        Sleep(milliseconds);
    }
}

/**
 * @brief Checks if any Alt key (Left, Right, or generic) is currently held down.
 * @return TRUE if an Alt key is pressed, FALSE otherwise.
 */
static BOOL IsAltKeyHeld(void) {
    // Check high-order bit (0x8000) for current pressed state
    return (GetAsyncKeyState(VK_MENU) & 0x8000) ||
           (GetAsyncKeyState(VK_LMENU) & 0x8000) ||
           (GetAsyncKeyState(VK_RMENU) & 0x8000);
}
