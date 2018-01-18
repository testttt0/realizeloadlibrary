// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "stdafx.h"
#ifdef _WIN64
#pragma comment(linker,"/INCLUDE:_tls_used")
#else
#pragma comment(linker,"/INCLUDE:__tls_used")
#endif // _WIN64


void NTAPI MY_TLS_CALLBACK(PVOID DllHandle, DWORD Reason, PVOID Reserved);


extern "C" __declspec(dllexport) void TestProc()//#《1》创建的动态链接库dll中的导出函数TestProc
{
	MessageBox(NULL, L"Test Proc!", NULL, MB_OK);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
//#dllmain功能只是为dll打开不同类型添加回调函数，以在自定义loadlibrary中看到打开过程。
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		MessageBox(NULL, L"Load!", NULL, MB_OK);
		break;
	}
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
	{
		MessageBox(NULL, L"UnLoad!", NULL, MB_OK);
		break;
	}
		break;
	}
	return TRUE;
}

//TLS回调函数测试
void NTAPI MY_TLS_CALLBACK(PVOID DllHandle, DWORD Reason, PVOID Reserved)
{
	if (Reason == DLL_PROCESS_ATTACH)
	{
		MessageBox(NULL, L"testdll被调用啦!", NULL, MB_OK);
	}
}

extern "C"
#ifdef _WIN64
#pragma const_seg(".CRT$XLX")
const
#else
#pragma data_seg(".CRT$XLX")
#endif
PIMAGE_TLS_CALLBACK pTLS_CALLBACKs[] = { MY_TLS_CALLBACK,0 };
//#dll中自动调用的回调函数，相当于so中写在init节区中的函数
#pragma data_seg()
#pragma const_seg()