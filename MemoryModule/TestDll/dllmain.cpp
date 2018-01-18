// dllmain.cpp : ���� DLL Ӧ�ó������ڵ㡣
#include "stdafx.h"
#ifdef _WIN64
#pragma comment(linker,"/INCLUDE:_tls_used")
#else
#pragma comment(linker,"/INCLUDE:__tls_used")
#endif // _WIN64


void NTAPI MY_TLS_CALLBACK(PVOID DllHandle, DWORD Reason, PVOID Reserved);


extern "C" __declspec(dllexport) void TestProc()//#��1�������Ķ�̬���ӿ�dll�еĵ�������TestProc
{
	MessageBox(NULL, L"Test Proc!", NULL, MB_OK);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
//#dllmain����ֻ��Ϊdll�򿪲�ͬ������ӻص������������Զ���loadlibrary�п����򿪹��̡�
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

//TLS�ص���������
void NTAPI MY_TLS_CALLBACK(PVOID DllHandle, DWORD Reason, PVOID Reserved)
{
	if (Reason == DLL_PROCESS_ATTACH)
	{
		MessageBox(NULL, L"testdll��������!", NULL, MB_OK);
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
//#dll���Զ����õĻص��������൱��so��д��init�����еĺ���
#pragma data_seg()
#pragma const_seg()