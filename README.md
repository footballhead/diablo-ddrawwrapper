## Synopsis

Graphics fix for Diablo 1. Fixes various graphical issues and adds some features. Also includes a graphical interface for options selection.

* Window and Full Screen Mode
* No Corrupted Desktop Colors
* Multiple Resolutions in Window Mode and Fullscreen
* V-Sync On/Off
* Screenshots Saved as PNG Files
* Proper Aspect Ratio in Full Screen Mode
* Easy In-game Menu Configuration Of Video Settings
* Should Work With All Versions and Expansions (Tested with Diablo 1.0, 1.09, Hellfire 1.0)

![Screenshot](screenshot.jpg)

## Motivation

This was an attempt to create a graphical fix for Daiblo 1 that does absolutely no modification of the game binary files. It only exists as a modified version of the DirectDraw library that is then loaded by the game at runtime. This allows it to potentially work with any version of Diablo including the expansion.

## Known Issues

* Slight Refresh Problems in the Main Menu
* Can Not Be Used For Play on Battle.net
* Can Only Access In-game Video Settings While Playing (not in main menu)

## Usage

Run the game with command line options
* /ddrawlog to write debug messages to ddraw_debug.log
* /ddrawdebug to write debug messages to a console

<del>Command line parameters are currently broken, requires code change to enable debug logging.</del>

> FWIW They work for me shrug.jpg. Using powershell and PR demo

Press the ~ key to open the in-game graphics setting menu. Use the arrow keys to navigate/change settings and enter to apply. Escape will exit the menu without applying settings.

Press Alt + Enter to toggle between fullscreen and window mode quickly

Press Print Screen to take a screenshot, it will be saved in the game run directoy with the same file name as a normal screenshot but in PNG format. All screenshots are saved in native game resolution (640x480).

## Source Code Notes

DirectDraw functions actually used by the game are implemented in a minimalist way. Many unused functions are partially implemented and commented for future development, some are left as unimplemented. This is not in any way a complete implementation of the DirectDraw library.

## Build Instructions (Visual Studio 2019)

First, install [DirectX SDK](https://www.microsoft.com/en-us/download/details.aspx?id=6812).

> If you get error S1023, see [this support topic](https://support.microsoft.com/en-ca/help/2728613/s1023-error-when-you-install-the-directx-sdk-june-2010)

Second, build ddrawwrapper:

1. Clone this repo **recursively** (to get Detours submodule). If you don't clone recursively then the build will fail.
2. Open `Project/ddrawwrapper.sln` in VS 2019
3. Make sure `C:\Diablo` exists. The build output is copied there for convenience, but if it can't copy then the build will fail.
4. Build all

Detours is built as a Pre-Build event.

### Debugging in VS 2019 CE

1. In the _Solution Explorer_ panel, right-click `ddrawwrapper` and select _Properties_ from the drop-down
2. Go to _Debugging_ > _Command_ and set it to "C:\Diablo\Diablo.exe"
3. Go to _Debugging_ > _Working Directory_ and set it to "C:\Diablo"
4. Click _OK_
5. Set up your breakpoints
6. On the top bar, click the green arrow _Local Windows Debugger_
