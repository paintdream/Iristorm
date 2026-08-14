#define _CRT_SECURE_NO_WARNINGS
#include "event_framework.h"
#include <iris_common.inl>
#include "plugins.inl"
using namespace iris;

// run this tutorial with:
//   lua.exe scripts/Main.lua --auto
// or interactively from any Lua host:
//   local framework = require("lua_event_framework").new()

extern "C"
#if defined(_MSC_VER)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int luaopen_lua_event_framework(lua_State* L) {
	iris_register_plugins(L);
	return iris_lua_t::forward(L, [](iris_lua_t lua) {
		return lua.make_type<event_framework_t>();
	});
}

// For starting from RunDll32 on Win32
#ifdef _WIN32
#include <filesystem>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

static HMODULE DllHandle = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
	switch (reason) {
		case DLL_PROCESS_ATTACH:
			DllHandle = hModule;
			break;
	}

	return TRUE;
}

#if _UNICODE
#define Main MainW
#endif

static std::string WideToUtf8(std::wstring_view str) {
	DWORD dwMinSize;
	dwMinSize = ::WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0, nullptr, nullptr);
	std::string ret;
	ret.resize(dwMinSize, 0);
	::WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), ret.data(), dwMinSize, nullptr, nullptr);
	return ret;
}

static std::wstring AnsiToWide(std::string_view str) {
	DWORD dwMinSize;
	dwMinSize = ::MultiByteToWideChar(CP_ACP, 0, str.data(), (int)str.size(), nullptr, 0);
	std::wstring ret;
	ret.resize(dwMinSize + 1, 0);
	::MultiByteToWideChar(CP_ACP, 0, str.data(), (int)str.size(), ret.data(), dwMinSize);
	return ret;
}

static std::wstring Utf8ToWide(std::string_view str) {
	DWORD dwMinSize;
	dwMinSize = ::MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
	std::wstring ret;
	ret.resize(dwMinSize + 1, 0);
	::MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), ret.data(), dwMinSize);
	return ret;
}

static std::wstring TStrToWide(LPCTSTR str) {
#if _UNICODE
	return str;
#else
	return AnsiToWide(str);
#endif
}

static std::string TStrToUtf8(LPCTSTR str) {
	return WideToUtf8(TStrToWide(str));
}

extern "C" __declspec(dllexport) void CALLBACK Main(HWND hwnd, HINSTANCE hinst, LPTSTR lpCmdLine, int nCmdShow) {
	if (lpCmdLine[0] == '\0')
		return;

	std::string scriptDirectory;
	LPTSTR fileName = ::PathFindFileName(lpCmdLine);
	if (fileName != lpCmdLine) {
		auto ch = *fileName;
		*fileName = 0;
		bool ret = ::SetCurrentDirectory(lpCmdLine);
		scriptDirectory = TStrToUtf8(lpCmdLine);

		if (!ret) {
			fprintf(stderr, "Cannot change directory!");
			return;
		}

		*fileName = ch;
	}

	std::string dllDirectory;
	WCHAR szDllPath[MAX_PATH * 2];
	::GetModuleFileNameW(DllHandle, szDllPath, MAX_PATH * 2);
	LPWSTR dllFileName = ::PathFindFileNameW(szDllPath);
	if (dllFileName != szDllPath) {
		*dllFileName = 0;
		::AddDllDirectory(szDllPath);
		dllDirectory = WideToUtf8(szDllPath);
	}

#ifdef _DEBUG
	if (::AllocConsole()) {
		(void)freopen("conout$", "w", stdout);
		(void)freopen("conout$", "w", stderr);
	}
#endif

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	int exit_code = 0;

	HANDLE file = ::CreateFile(fileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file != INVALID_HANDLE_VALUE) {
		DWORD size = ::GetFileSize(file, nullptr);
		if (size != 0xFFFFFFFF) {
			std::string str;
			str.resize(size);

			if (::ReadFile(file, str.data(), size, nullptr, nullptr)) {
				if (luaL_loadstring(L, str.data()) == LUA_OK) {
					lua_pushlstring(L, scriptDirectory.data(), scriptDirectory.size());
					lua_pushlstring(L, dllDirectory.data(), dllDirectory.size());

					if (lua_pcall(L, 2, LUA_MULTRET, 0) != LUA_OK) {
						fprintf(stderr, "Lua Error: %s\n", lua_tostring(L, -1));
						exit_code = 1;
					} else {
						// use the script's first return value as the process
						// exit code, if it is a number. This lets --auto
						// scripts report PASS/FAIL without os.exit(): calling
						// os.exit() from Lua would bypass lua_close() below,
						// leaking the Lua state and tripping the iris block
						// allocator's clean-exit assertion (see docs/README.md
						// "exit discipline").
						if (lua_isnumber(L, -1)) {
							exit_code = static_cast<int>(lua_tonumber(L, -1));
						}
					}
				} else {
					fprintf(stderr, "Lua Error: %s\n", lua_tostring(L, -1));
					exit_code = 1;
				}
			} else {
				fprintf(stderr, "Lua Error: ReadFile error!\n");
				exit_code = 1;
			}
		} else {
			fprintf(stderr, "Lua Error: Too large file!\n");
			exit_code = 1;
		}

		::CloseHandle(file);
	} else {
		fprintf(stderr, "Lua Error: Invalid entry file!\n");
		exit_code = 1;
	}

	// close the Lua state FIRST so that all userdata is collected, coroutines
	// are released and every iris block is returned to its allocator; only
	// then exit with the script's code (static destructors run cleanly).
	lua_close(L);
	if (exit_code != 0) {
		exit(exit_code);
	}
}

#endif
