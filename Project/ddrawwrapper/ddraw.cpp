#pragma comment(linker, "/EXPORT:DirectDrawCreate=_DirectDrawCreate@12")

#include "DirectDrawWrapper.h"
#include "resource.h"
#include "detours.h"
#include <stdio.h>
#include <math.h>

// Main thread ddrawwrapper object
IDirectDrawWrapper *lpDD = NULL;
// Original setcursorpos function pointer
static BOOL (WINAPI *TrueSetCursorPos)(int,int) = SetCursorPos;
static HMODULE (WINAPI *TrueLoadLibraryA)(LPCSTR) = GetModuleHandleA;

// Are we in the settings menu
BOOL inMenu;
// Dll start time
DWORD start_time;
// The level of debug to display
int debugLevel;
//debug display mode (-1 = none, 0 = console, 1 = file)
int debugDisplay;
//the debug file handle
FILE *debugFile;

// Dll hmodule
HMODULE hMod;

/* Helper function for throwing debug/error messages
 *
 * int level      - Debug level
 * char *location - Message location
 * char *message  - Message
 */
void debugMessage(int level, char *location, char *message)
{
	// If above the current level then skip totally
	if(level > debugLevel) return;

    // Calculate HMS
	DWORD cur_time = GetTickCount() - start_time;
	long hours = (long)floor((double)cur_time / (double)3600000.0);
	cur_time -= (hours * 3600000);
	int minutes = (int)floor((double)cur_time / (double)60000.0);
	cur_time -= (minutes * 60000);
	double seconds = (double)cur_time / (double)1000.0;

    // Build error message
	char text[4096] = "\0";
	if(level == 0)
	{
		sprintf_s(text, 4096, "%d:%d:%#.1f ERR %s %s\n", hours, minutes, seconds, location, message);
	}
	else if(level == 1)
	{
		sprintf_s(text, 4096, "%d:%d:%#.1f WRN %s %s\n", hours, minutes, seconds, location, message);
	}
	else if(level == 2)
	{
		sprintf_s(text, 4096, "%d:%d:%#.1f INF %s %s\n", hours, minutes, seconds, location, message);
	}
    // Output and flush
	printf_s(text);
	fflush(stdout);
}

// Override function for cursor position
BOOL WINAPI OverrideSetCursorPos(int X, int Y)
{
	// If ddraw object exists and windowed mode
	if(lpDD != NULL && lpDD->isWindowed)
	{
		// X,Y are relative to client area within the code
		// Get client area location
		POINT cpos;
		cpos.x = 0;
		cpos.y = 0;
		ClientToScreen(lpDD->hWnd, &cpos);

		// Calculate correct cursor offset and move
		BOOL res = TrueSetCursorPos(cpos.x + X, cpos.y + Y);
		return res;
	}
	return TrueSetCursorPos(X, Y);
}

// Override function for load library
/*HMODULE WINAPI OverrideLoadLibraryA(LPCSTR lpModuleName)
{
	printf_s("%s\n", lpModuleName);
	return TrueLoadLibraryA(lpModuleName);
}*/

// Pretty standard winproc
LRESULT CALLBACK WndProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        // On syskey down
		case WM_SYSKEYDOWN:
			// ALT+ENTER trap keydown
			if (wParam == VK_RETURN)
			{
				return 0;
			}
			break;
        // On syskey up
		case WM_SYSKEYUP:
			// ALT+ENTER trap keyup
			if (wParam == VK_RETURN)
			{
				// If DDW exists
				if(lpDD != NULL)
				{
					lpDD->ToggleFullscreen();
				}
				return 0;
			}
			break;
        // On keydown
		case WM_KEYDOWN:
			// Overload printscreen(save a snapshot of the current screen
			if(wParam == VK_SNAPSHOT)
			{
				return 0;
			}
			// Always pass ~ to menu
			if(wParam == VK_OEM_3)
			{
				return 0;
			}
			// Everything gets passed if we are in the menu
			if(inMenu)
			{
				return 0;
			}
			break;
        // On keyup
		case WM_KEYUP:
			// Overload printscreen(save a snapshot of the current screen)
			if(!inMenu && wParam == VK_SNAPSHOT)
			{
				lpDD->DoSnapshot();
				return 0;
			}
			// Always pass ~ to menu
			if(wParam == VK_OEM_3)
			{
				// Pass to menu and set inMenu to result
				inMenu = lpDD->MenuKey(VK_OEM_3);
				return 0;
			}
			// Everything gets passed if we are in the menu
			if(inMenu)
			{
				// Pass to menu and set inMenu to result
				inMenu = lpDD->MenuKey(wParam);
				return 0;
			}
			break;
        // On destroy window
		case WM_DESTROY:
			// Restore the orignal window proc if we have it
			if(lpDD->lpPrevWndFunc != NULL)
			{
				SetWindowLong(hwnd, GWL_WNDPROC, (LONG)lpDD->lpPrevWndFunc);
			}
			break;
    }

	// If focused then prevent cursor from leaving window.
	// I would have expected to only call ClipCursor once but I guess we have to call it every tick...
	if (GetFocus() == hwnd && lpDD->captureMouse) {
		// Even though these points are hardcoded, they seem to work regardless of the chosen resolution
		POINT topLeft = { 0, 0 };
		POINT bottomRight = { 640, 480 };
		if (ClientToScreen(hwnd, &topLeft) && ClientToScreen(hwnd, &bottomRight)) {
			RECT clientBoundsInScreenCoords = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
			ClipCursor(&clientBoundsInScreenCoords);
		}
	}

	// Call original windiow proc by default
    return CallWindowProc(lpDD->lpPrevWndFunc, hwnd, message, wParam, lParam);
}

// From Pre-ablo
template <typename T>
bool patch(DWORD addr_to_patch, T const& new_val)
{
	// If we don't turn the .text address to be PAGE_EXECUTE_READWRITE then the game crashes
	DWORD oldProtect = 0;
	if (!VirtualProtect(reinterpret_cast<void*>(addr_to_patch), sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		printf("%s: failed to mark address +xrw: 0x%X\n", __func__, addr_to_patch);
		return false;
	}
	// Do the actual patch
	*(T*)(addr_to_patch) = new_val;
	return true;
}

// From Pre-ablo
bool nop(DWORD address_start, unsigned __int32 address_end)
{
	DWORD oldProtect = 0;
	if (!VirtualProtect((void*)address_start, address_end - address_start, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		printf("%s: failed to mark address +xrw: 0x%X\n", __func__, address_start);
		return false;
	}
	// Do the actual patch
	memset((void*)address_start, 0x90, address_end - address_start); // 0x90 is single byte NOP
	return true;
}

// From Pre-abo
template <typename T>
bool patch_call(DWORD address, T fn)
{
	DWORD oldProtect = 0;
	if (!VirtualProtect((void*)address, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		printf("%s: failed to mark address +xrw: 0x%X\n", __func__, address);
		return false;
	}
	*(BYTE*)(address) = 0xE8; // relative call opcode
	*(DWORD*)(address + 1) = reinterpret_cast<DWORD>(fn) - (address + 5);
	return true;
}

// From Pre-ablo
bool patch_bytes(DWORD address, BYTE const* patch, size_t size)
{
	DWORD oldProtect = 0;
	if (!VirtualProtect((void*)address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		printf("%s: failed to mark address +xrw: 0x%X\n", __func__, address);
		return false;
	}
	memcpy((void*)address, patch, size);
	return true;
}

auto const DRLG_L2PlaceMiniSet = reinterpret_cast<BOOL(__fastcall*)(BYTE * miniset, int tmin, int tmax, int cx, int cy, BOOL setview, int ldir)>(0x0047C790);
auto const GiveGoldCheat = reinterpret_cast<void(__fastcall*)(void)>(0x0042CB9C);
auto const MaxSpellsCheat = reinterpret_cast<void(__fastcall*)(void)>(0x0042CF13);
auto const NextPlrLevel = reinterpret_cast<void(__fastcall*)(int)>(0x0046B9CC);
auto const StartGame = reinterpret_cast<BOOL (__fastcall*)(BOOL, BOOL)>(0x00490F61);
auto const LoadLvlGFX = reinterpret_cast<void(__fastcall*)(void)>(0x004931A7);
// Differs from Devilution in that additional debug info (3rd param is lineno, 4th is func)
auto const LoadFileInMem = reinterpret_cast<BYTE*(__fastcall*)(char*, DWORD*, DWORD, const char*)>(0x00490A37);

auto const currlevel = reinterpret_cast<BYTE* const>(0x0057CDA8);
auto const USTAIRS = reinterpret_cast<BYTE* const>(0x004DA5C8);
auto const WARPSTAIRS = reinterpret_cast<BYTE* const>(0x004DA618);
auto const DSTAIRS = reinterpret_cast<BYTE* const>(0x004DA5F0);
auto const ViewY = reinterpret_cast<DWORD* const>(0x00590B18);
auto const ViewX = reinterpret_cast<DWORD* const>(0x00590B1C);
auto const myplr = reinterpret_cast<int* const>(0x0062D88C);
auto const gszHero = reinterpret_cast<char* const>(0x0061D630);
auto const gbMaxPlayers = reinterpret_cast<BYTE* const>(0x006000E0);
auto const leveltype = reinterpret_cast<BYTE* const>(0x0057CDA4);
auto const pDungeonCels = reinterpret_cast<BYTE** const>(0x0057F6CC);
auto const pMegaTiles = reinterpret_cast<BYTE** const>(0x00578C90);
auto const pLevelPieces = reinterpret_cast<BYTE** const>(0x00578C94);
auto const pSpecialCels = reinterpret_cast<BYTE** const>(0x005880E4);

HMODULE diabloui_dll = NULL;
// The void*s are functions but the actual signatures shouldn't matter
// Since the entire diabloui.dll is loaded in memory, we can just call the address without having to GetProcAddress it
auto const UiSelHeroSingDialog = reinterpret_cast<BOOL(__stdcall *)(void*, void*, void*, void*, int*, char*)>(0x100070E0);

// This is from devilution with some beta-specific modifications
// entry is a param to the calling function
BOOL __fastcall catacombs_stairs_fix(int entry)
{
	// This is a local from the calling function... so we 
	BOOL doneflag;
	if (entry == 0) {
		doneflag = DRLG_L2PlaceMiniSet(USTAIRS, 1, 1, -1, -1, 1, 0);
		if (doneflag) {
			doneflag = DRLG_L2PlaceMiniSet(DSTAIRS, 1, 1, -1, -1, 0, 1); // This is an addition and why we needed to redirect to our own function
			if (doneflag && *currlevel == 5) {
				doneflag = DRLG_L2PlaceMiniSet(WARPSTAIRS, 1, 1, -1, -1, 0, 6);
			}
		}
		(*ViewY)++; // Different
	}
	else if (entry == 1) {
		doneflag = DRLG_L2PlaceMiniSet(USTAIRS, 1, 1, -1, -1, 0, 0);
		if (doneflag) {
			doneflag = DRLG_L2PlaceMiniSet(DSTAIRS, 1, 1, -1, -1, 1, 1); // This is an addition and why we needed to redirect to our own function
			if (doneflag && *currlevel == 5) {
				doneflag = DRLG_L2PlaceMiniSet(WARPSTAIRS, 1, 1, -1, -1, 0, 6);
			}
		}
		(*ViewX)++; // Different
	}
	else {
		doneflag = DRLG_L2PlaceMiniSet(USTAIRS, 1, 1, -1, -1, 0, 0);
		if (doneflag) {
			doneflag = DRLG_L2PlaceMiniSet(DSTAIRS, 1, 1, -1, -1, 0, 1); // This is an addition and why we needed to redirect to our own function
			if (doneflag && *currlevel == 5) {
				doneflag = DRLG_L2PlaceMiniSet(WARPSTAIRS, 1, 1, -1, -1, 1, 6);
			}
		}
		(*ViewY)++; // Different
	}
	return doneflag;
}

void __fastcall z_hook()
{
	GiveGoldCheat();
	MaxSpellsCheat();
	NextPlrLevel(*myplr);
}

void __fastcall singleplayer_menu_hook()
{
	int dlgresult;
	printf("Single player!\n");
	/*UiSelHeroSingDialog(
		pfile_ui_set_hero_infos,
		pfile_ui_save_create,
		pfile_delete_save,
		pfile_ui_set_class_stats,
		&dlgresult,
		gszHero)*/
	*gbMaxPlayers = 1;
	if (!UiSelHeroSingDialog((void*)0x00436E74, (void*)0x004372AA, (void*)0x004374EA, (void*)0x00437209, &dlgresult, gszHero)) {
		MessageBox(NULL, TEXT("UiSelHeroSingDialog failed"), TEXT("Error"), 0);
		ExitProcess(0);
	}
	printf("dlgresult=%d\n", dlgresult);
	if (dlgresult == 2) {
		StartGame(TRUE, TRUE);
	}
}

void LoadLvlGFX_wrapper()
{
	if (*leveltype == 3) { // 3 == caves
		*pDungeonCels = LoadFileInMem("Levels\\L3Data\\L3.CEL", NULL, __LINE__, __FUNCTION__);
		*pMegaTiles = LoadFileInMem("Levels\\L3Data\\L3.TIL", NULL, __LINE__, __FUNCTION__);
		*pLevelPieces = LoadFileInMem("Levels\\L3Data\\L3.MIN", NULL, __LINE__, __FUNCTION__);
		*pSpecialCels = LoadFileInMem("Levels\\L1Data\\L1S.CEL", NULL, __LINE__, __FUNCTION__);
		return;
	}
	LoadLvlGFX();
}

// Main dll entry
BOOL APIENTRY DllMain( HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	LONG error;

	switch (ul_reason_for_call)
	{
	// Initial process attach
	case DLL_PROCESS_ATTACH: {
		// Remove CD requirement, allowing local DIABDAT.MPQ
		// Change CD parameter on SFileOpenArchive from 1 to 0.
		// Requires modifying push 1 to push 0
		patch<BYTE>(0x0042DD55 + 1, 0);

		// Generate stairs down in catacombs
		// Remove old code
		nop(0x0048118C, 0x004812B2);
		// Prepare to call our code
		constexpr BYTE mov_ecx_entry[] = { 0x8B, 0x4D, 0xF0 };
		patch_bytes(0x0048118C, mov_ecx_entry, sizeof(mov_ecx_entry));
		// call our code
		patch_call(0x0048118C + sizeof(mov_ecx_entry), catacombs_stairs_fix);
		// Store return value
		constexpr BYTE mov_doneflag_eax[] = { 0x89, 0x45, 0xFC };
		patch_bytes(0x0048118C + sizeof(mov_ecx_entry) + 5, mov_doneflag_eax, sizeof(mov_doneflag_eax));

		// Call our own function when Z is pressed
		// Remove existing code
		nop(0x00492C78, 0x00492C9E);
		// call our code
		patch_call(0x00492C78, z_hook);

		// Make MaxSpellsCheat give all the spells (by removing "can learn" check)
		nop(0x0042CF35, 0x0042CF49);

		// Allow using catacombs warp in single player by noping multi-player check
		nop(0x0047B6C2, 0x0047B6D2);

		// nop old single player button behavor
		nop(0x0043A21B, 0x0043A228);
		// Make single player option call our func
		patch_call(0x0043A21B, singleplayer_menu_hook);
		// TODO Storm changes to make this work

		// bone spirit uses MFILE_FIRERUN
		patch<BYTE>(0x004C60FE, 0x24);

		// poison water
		// Extend LoadLvlGFX to include caves tileset
		patch_call(0x00493502, LoadLvlGFX_wrapper);
		// Remove reward (crashes because unique item #88 doesn't exist)
		nop(0x0045987F, 0x004598B3);

		// diabloui modification
		// Make Single Player text gold instead of gray
		patch<BYTE>(0x100034E4 + 1, 6);
		// Allow it to be selectable by noping the code that disables it
		nop(0x100034F4, 0x10003505);

		// Store module handle
		hMod = hModule;

		// Set default variables
		debugLevel = 0;
		debugDisplay = -1;

		// Set time start
		start_time = GetTickCount();

		// Not in menu to start
		inMenu = false;

		// Retrieve command line arguments
		LPWSTR* szArgList;
		int argCount;
		szArgList = CommandLineToArgvW(GetCommandLine(), &argCount);

		// If arguments
		if (szArgList != NULL)
		{
			for (int i = 0; i < argCount; i++)
			{
				// If debug
				if (wcscmp(szArgList[i], L"/ddrawdebug") == 0) {
					debugDisplay = 0;
					debugLevel = 2;
					// Create the debug console
					AllocConsole();
					// Redirect stdout to console
					freopen("CONOUT$", "wb", stdout);
					break;
				}
				// If ddrawlog
				else if (wcscmp(szArgList[i], L"/ddrawlog") == 0) {
					debugDisplay = 1;
					debugLevel = 2;
					// Redireect stdout to file
					char curPath[MAX_PATH];
					char filename[MAX_PATH];
					GetCurrentDirectoryA(MAX_PATH, curPath);
					sprintf_s(filename, MAX_PATH, "%s\\ddraw_debug.log", curPath);
					freopen_s(&debugFile, filename, "wb", stdout);
					break;
				}
			}
		}
		LocalFree(szArgList);

		// Hook setcursorpos
		DetourRestoreAfterWith();
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&(PVOID&)TrueSetCursorPos, OverrideSetCursorPos);
		//DetourAttach(&(PVOID&)TrueLoadLibraryA, OverrideLoadLibraryA);
		error = DetourTransactionCommit();
		if (error == NO_ERROR) {
			debugMessage(2, "DllMain(DLL_PROCESS_ATTACH)", "Successfully detoured SetCursorPos");
		}
		else {
			debugMessage(1, "DllMain(DLL_PROCESS_ATTACH)", "Failed to detour SetCursorPos");
		}
		break;
	}
	case DLL_THREAD_ATTACH:
		// Do nothing on thread attach
		break;
	case DLL_THREAD_DETACH:
		// Do nothing on thread detach
		break;
	case DLL_PROCESS_DETACH:		
		ClipCursor(NULL);

		// Delete DirectDrawWrapper object
		delete lpDD;

		// Detach function hook
		DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)TrueSetCursorPos, OverrideSetCursorPos);
        error = DetourTransactionCommit();

        
		//cleanup debug console or file if exists
		if(debugDisplay == 0)
		{
			FreeConsole();
		}
		else if(debugDisplay == 1)
		{
			fclose(debugFile);
		}
        
		break;
	}
	return TRUE;
}

// Emulated direct draw create
extern HRESULT WINAPI DirectDrawCreate(GUID FAR* lpGUID, LPDIRECTDRAW FAR* lplpDD, IUnknown FAR* pUnkOuter)
{
	// Create directdraw object
	lpDD = new IDirectDrawWrapper();
	if(lpDD == NULL) 
	{
		debugMessage(0, "DirectDrawCreate", "Failed to create IDirectDrawWrapper.");
		return DDERR_OUTOFMEMORY;  // Simulate OOM error
	}
	
	// Initialize the ddraw object with new wndproc
	HRESULT hr = lpDD->WrapperInitialize(&WndProc, hMod);
	// If error then return error(message will have been taken care of already)
	if(hr != DD_OK) return hr;

	// Set return pointer to the newly created DirectDrawWrapper interface
	*lplpDD = (LPDIRECTDRAW)lpDD;
	
	// Return success
	return DD_OK;
}