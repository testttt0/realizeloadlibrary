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
		printf("���ļ�����%d\n", GetLastError());
	}
	DWORD dwFileSize = 0;
	dwFileSize = GetFileSize(hDll, NULL);
	//#dll�ļ���С
	if (dwFileSize <= 0)
	{
		printf("����ļ���Сʧ��!\n");
		return -1;
	}
	unsigned char* DllBuffer = (unsigned char*)malloc(dwFileSize);
	DWORD dwDataLength = 0;
	if (!ReadFile(hDll, DllBuffer, dwFileSize, &dwDataLength, NULL))
	{//#��1����ȡdll�ļ���malloc���ڴ洦
		printf("��ȡ�ļ�����%d\n", GetLastError());
		return -1;
	}

	HMEMORYMODULE hModule = MemoryLoadLibrary(DllBuffer, dwDataLength);
	//#������malloc���ڴ���ʼ��ַ��0
	pfnTestProc TestProc = (pfnTestProc)MemoryGetProcAddress(hModule, "TestProc");
	TestProc();

	MemoryFreeLibrary(hModule);
	return 0;
}