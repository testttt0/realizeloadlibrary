#include "stdafx.h"
#include "MemoryModule.h"

typedef void(WINAPI *pfnTestProc)();

int main()
{
#ifdef _WIN64
	HANDLE hDll = CreateFile(L"..//x64//Debug//TestDll.dll", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
	//#
#else
	HANDLE hDll = CreateFile(L"..//Debug//TestDll.dll", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
#endif // _WIN64

	
	if (hDll == INVALID_HANDLE_VALUE)
	{
		printf("打开文件错误：%d\n", GetLastError());
	}
	DWORD dwFileSize = 0;
	dwFileSize = GetFileSize(hDll, NULL);
	//#dll文件大小
	if (dwFileSize <= 0)
	{
		printf("获得文件大小失败!\n");
		return -1;
	}
	unsigned char* DllBuffer = (unsigned char*)malloc(dwFileSize);
	DWORD dwDataLength = 0;
	if (!ReadFile(hDll, DllBuffer, dwFileSize, &dwDataLength, NULL))
	{//#《1》读取dll文件到malloc的内存处
		printf("读取文件错误：%d\n", GetLastError());
		return -1;
	}

	HMEMORYMODULE hModule = MemoryLoadLibrary(DllBuffer, dwDataLength);
	//#参数是malloc的内存起始地址和0
	pfnTestProc TestProc = (pfnTestProc)MemoryGetProcAddress(hModule, "TestProc");
	TestProc();

	MemoryFreeLibrary(hModule);
	return 0;
}