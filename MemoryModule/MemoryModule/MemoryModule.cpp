#include "stdafx.h"
#include "MemoryModule.h"

//��������Ƿ���ȷ��ͨ���������ݴ�С�Ƿ��ڷ�Χ��
static BOOL CheckSize(size_t Size, size_t Expected)
{
	if (Size < Expected)
	{
		SetLastError(ERROR_INVALID_DATA);
		return FALSE;
	}

	return TRUE;
}

static inline size_t AlignValueUp(size_t Value, size_t Alignment)
{
	return (Value + Alignment - 1) & ~(Alignment - 1);
}

static inline void* OffsetPointer(void* Data, ptrdiff_t Offset)
{
	return (void*)((uintptr_t)Data + Offset);
}

static inline uintptr_t AlignValueDown(uintptr_t Value, uintptr_t Alignment)
{
	return Value & ~(Alignment - 1);
}

static inline LPVOID AlignAddressDown(LPVOID Address, uintptr_t Alignment)
{
	return (LPVOID)AlignValueDown((uintptr_t)Address, Alignment);
}


//VirtualAlloc
LPVOID MemoryDefaultAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD dwAllocationType, DWORD dwProtect, void* pUserData)
{
	UNREFERENCED_PARAMETER(pUserData);
	return VirtualAlloc(lpAddress, dwSize, dwAllocationType, dwProtect);
}

//Free
BOOL MemoryDefaultFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType, void* pUserData)
{
	UNREFERENCED_PARAMETER(pUserData);
	return VirtualFree(lpAddress, dwSize, dwFreeType);
}

//LoadLibraryA
HCUSTOMMODULE MemoryDefaultLoadLibrary(LPCSTR szFileName, void *pUserData)
{
	HMODULE hModule;
	UNREFERENCED_PARAMETER(pUserData);
	hModule = LoadLibraryA(szFileName);
	if (hModule == NULL)
	{
		return NULL;
	}

	return (HCUSTOMMODULE)hModule;
}

//GetProcAddress
FARPROC MemoryDefaultGetProcAddress(HCUSTOMMODULE hModule, LPCSTR szProcName, void *pUserData)
{
	UNREFERENCED_PARAMETER(pUserData);
	return (FARPROC)GetProcAddress((HMODULE)hModule, szProcName);
}

//FreeLibrary
void MemoryDefaultFreeLibrary(HCUSTOMMODULE hModule, void *pUserData)
{
	UNREFERENCED_PARAMETER(pUserData);
	FreeLibrary((HMODULE)hModule);
}

//���ļ��н�����Сת��Ϊ�ڴ��н�����С
static SIZE_T GetRealSectionSize(PMEMORYMODULE Module, PIMAGE_SECTION_HEADER Section_Header)
{
	DWORD dwSize = Section_Header->SizeOfRawData;
	if (dwSize == 0)
	{
		if (Section_Header->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)        //���а����ѳ�ʼ������
		{
			dwSize = Module->NtHeaders->OptionalHeader.SizeOfInitializedData;
		}
		else if (Section_Header->Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) //���а���δ��ʼ������
		{
			dwSize = Module->NtHeaders->OptionalHeader.SizeOfUninitializedData;
		}
	}
	return (SIZE_T)dwSize;
}


//������
static BOOL CopySections(const unsigned char *Data, size_t Size, PIMAGE_NT_HEADERS Nt_Headers, PMEMORYMODULE Module)
{
	int i, SizeOfSection;
	unsigned char *CodeBase = Module->pCodeBase;
	unsigned char *SectionBase;        //������ַ
	PIMAGE_SECTION_HEADER Section_Header = IMAGE_FIRST_SECTION(Module->NtHeaders);
	for (i = 0; i<Module->NtHeaders->FileHeader.NumberOfSections; i++, Section_Header++)
	{
		//�����н���������û����
		if (Section_Header->SizeOfRawData == 0)
		{
			//�ڴ�СΪ0������£�ֱ�Ӱ������ȶ���
			SizeOfSection = Nt_Headers->OptionalHeader.SectionAlignment;
			if (SizeOfSection > 0)
			{
				SectionBase = (unsigned char *)Module->MyAlloc(CodeBase + Section_Header->VirtualAddress,
					SizeOfSection,
					MEM_COMMIT,
					PAGE_READWRITE,
					Module->pUserData);
				if (SectionBase == NULL)
				{
					return FALSE;
				}


				SectionBase = CodeBase + Section_Header->VirtualAddress;

				Section_Header->Misc.PhysicalAddress = (DWORD)((uintptr_t)SectionBase & 0xffffffff);
				memset(SectionBase, 0, SizeOfSection);
			}

			// ����Ϊ��
			continue;
		}

		if (!CheckSize(Size, Section_Header->PointerToRawData + Section_Header->SizeOfRawData))
		{
			return FALSE;
		}

		//������������
		SectionBase = (unsigned char *)Module->MyAlloc(CodeBase + Section_Header->VirtualAddress,
			Section_Header->SizeOfRawData,
			MEM_COMMIT,
			PAGE_READWRITE,
			Module->pUserData);
		if (SectionBase == NULL)
		{
			return FALSE;
		}

		// Always use position from file to support alignments smaller
		// than page Size (allocation above will align to page Size).
		SectionBase = CodeBase + Section_Header->VirtualAddress;
		memcpy(SectionBase, Data + Section_Header->PointerToRawData, Section_Header->SizeOfRawData);
		// NOTE: On 64bit systems we truncate to 32bit here but expand
		// again later when "PhysicalAddress" is used.
		Section_Header->Misc.PhysicalAddress = (DWORD)((uintptr_t)SectionBase & 0xffffffff);
	}

	return TRUE;
}

//�ض���
static BOOL PerformBaseRelocation(PMEMORYMODULE Module, ptrdiff_t Delta)
{
	unsigned char *CodeBase = Module->pCodeBase;
	PIMAGE_BASE_RELOCATION Relocation;

	//����ض����Ŀ¼��
	PIMAGE_DATA_DIRECTORY BaseRelocDirectory = GET_HEADER_DICTIONARY(Module, IMAGE_DIRECTORY_ENTRY_BASERELOC);
	if (BaseRelocDirectory->Size == 0)
	{
		return (Delta == 0);
	}

	Relocation = (PIMAGE_BASE_RELOCATION)(CodeBase + BaseRelocDirectory->VirtualAddress);
	while (Relocation->VirtualAddress > 0)
	{
		DWORD i;

		unsigned char  *RelocationBase = CodeBase + Relocation->VirtualAddress;   //�ض����RVA
		unsigned short *RelocationInfo = (unsigned short*)OffsetPointer(Relocation, IMAGE_SIZEOF_BASE_RELOCATION);
		for (i = 0; i<((Relocation->SizeOfBlock - IMAGE_SIZEOF_BASE_RELOCATION) / 2); i++, RelocationInfo++)
		{
			// ��4λΪType
			int Type = *RelocationInfo >> 12;
			// ��12λΪƫ��
			int Offset = *RelocationInfo & 0xfff;

			switch (Type)
			{
			case IMAGE_REL_BASED_ABSOLUTE:
				//��������壬��������
				break;

			case IMAGE_REL_BASED_HIGHLOW:
				//32λ����Ҫ����
			{
				DWORD *PatchAddrHL = (DWORD *)(RelocationBase + Offset);
				*PatchAddrHL += (DWORD)Delta;
			}
			break;

#ifdef _WIN64
			case IMAGE_REL_BASED_DIR64:
				//64λ
			{
				ULONGLONG *PatchAddr64 = (ULONGLONG *)(RelocationBase + Offset);
				*PatchAddr64 += (ULONGLONG)Delta;
			}
			break;
#endif

			default:

				break;
			}
		}

		// ��һ���ض���Block
		Relocation = (PIMAGE_BASE_RELOCATION)OffsetPointer(Relocation, Relocation->SizeOfBlock);
	}
	return TRUE;
}

//�����
static BOOL BuildImportTable(PMEMORYMODULE Module)
{
	unsigned char *CodeBase = Module->pCodeBase;
	PIMAGE_IMPORT_DESCRIPTOR ImportDescriptor;
	BOOL bOk = TRUE;
	//��õ�������
	PIMAGE_DATA_DIRECTORY ImportDirectory = GET_HEADER_DICTIONARY(Module, IMAGE_DIRECTORY_ENTRY_IMPORT);
	if (ImportDirectory->Size == 0)
	{
		return TRUE;
	}

	ImportDescriptor = (PIMAGE_IMPORT_DESCRIPTOR)(CodeBase + ImportDirectory->VirtualAddress);  //������RVA
	for (; !IsBadReadPtr(ImportDescriptor, sizeof(IMAGE_IMPORT_DESCRIPTOR)) && ImportDescriptor->Name; ImportDescriptor++)
	{
		uintptr_t *ThunkRef;
		FARPROC   *FuncRef;
		HCUSTOMMODULE *v1;
		HCUSTOMMODULE hModule = Module->MyLoadLibrary((LPCSTR)(CodeBase + ImportDescriptor->Name), Module->pUserData);
		if (hModule == NULL)
		{
			SetLastError(ERROR_MOD_NOT_FOUND);
			bOk = FALSE;
			break;
		}

		v1 = (HCUSTOMMODULE *)realloc(Module->pModules, (Module->nNumberOfModules + 1)*(sizeof(HCUSTOMMODULE)));
		if (v1 == NULL)
		{
			Module->MyFreeLibrary(hModule, Module->pUserData);
			SetLastError(ERROR_OUTOFMEMORY);
			bOk = FALSE;
			break;
		}
		Module->pModules = v1;

		Module->pModules[Module->nNumberOfModules++] = hModule;
		if (ImportDescriptor->OriginalFirstThunk)
		{
			//ע�������˫�Žṹ��OriginalFirstThunkָ��INT��FirstThunkָ��IAT�������������еı���ָ��ͬһ��������ַ
			ThunkRef = (uintptr_t *)(CodeBase + ImportDescriptor->OriginalFirstThunk); //INT
			FuncRef = (FARPROC *)(CodeBase + ImportDescriptor->FirstThunk);           //IAT
		}
		else
		{
			// ��INT,�еĳ���ֻ����һ���ţ���Borland��˾��Tlinkֻ������2
			ThunkRef = (uintptr_t *)(CodeBase + ImportDescriptor->FirstThunk);
			FuncRef = (FARPROC *)(CodeBase + ImportDescriptor->FirstThunk);
		}
		for (; *ThunkRef; ThunkRef++, FuncRef++)
		{
			if (IMAGE_SNAP_BY_ORDINAL(*ThunkRef))
			{
				*FuncRef = Module->MyGetProcAddress(hModule, (LPCSTR)IMAGE_ORDINAL(*ThunkRef), Module->pUserData);
			}
			else
			{
				//INT
				PIMAGE_IMPORT_BY_NAME ThunkData = (PIMAGE_IMPORT_BY_NAME)(CodeBase + (*ThunkRef));
				*FuncRef = Module->MyGetProcAddress(hModule, (LPCSTR)&ThunkData->Name, Module->pUserData);
			}
			if (*FuncRef == 0)
			{
				bOk = FALSE;
				break;
			}
		}

		if (!bOk)
		{
			//������ɹ����ͷŶ�̬��
			Module->MyFreeLibrary(hModule, Module->pUserData);
			SetLastError(ERROR_PROC_NOT_FOUND);
			break;
		}
	}

	return bOk;
}

//�����ڴ�ҳ���Ե�
static BOOL FinalizeSection(PMEMORYMODULE Module, PSECTIONFINALIZEDATA SectionData)
{
	DWORD dwProtect, dwOldProtect;
	BOOL  bExecutable;
	BOOL  bReadable;
	BOOL  bWriteable;

	if (SectionData->Size == 0)
	{
		return TRUE;
	}
	//���������ڽ��������󽫱���������.reloc
	if (SectionData->dwCharacteristics & IMAGE_SCN_MEM_DISCARDABLE)
	{
		//�������ݲ�����Ҫ���ͷ�
		if (SectionData->lpAddress == SectionData->lpAlignedAddress &&
			(SectionData->bIsLast ||
				Module->NtHeaders->OptionalHeader.SectionAlignment == Module->dwPageSize ||
				(SectionData->Size % Module->dwPageSize) == 0)
			)
		{

			Module->MyFree(SectionData->lpAddress, SectionData->Size, MEM_DECOMMIT, Module->pUserData);
		}
		return TRUE;
	}

	//
	bExecutable = (SectionData->dwCharacteristics & IMAGE_SCN_MEM_EXECUTE) != 0; //��ִ��
	bReadable = (SectionData->dwCharacteristics & IMAGE_SCN_MEM_READ) != 0;      //�ɶ�
	bWriteable = (SectionData->dwCharacteristics & IMAGE_SCN_MEM_WRITE) != 0;    //��д
	dwProtect = ProtectionFlags[bExecutable][bReadable][bWriteable];
	if (SectionData->dwCharacteristics & IMAGE_SCN_MEM_NOT_CACHED)   //�������ݲ��ᾭ������
	{
		dwProtect |= PAGE_NOCACHE;
	}

	// �ı��ڴ�ҳ����
	if (VirtualProtect(SectionData->lpAddress, SectionData->Size, dwProtect, &dwOldProtect) == 0)
	{

		return FALSE;
	}

	return TRUE;
}

//��һЩ�������ļ�����ֵת��Ϊ�ڴ�����ֵ
static BOOL FinalizeSections(PMEMORYMODULE Module)
{
	int i;
	PIMAGE_SECTION_HEADER Section_Header = IMAGE_FIRST_SECTION(Module->NtHeaders);
#ifdef _WIN64
	//�п��ܳ�32λ
	uintptr_t ImageOffset = ((uintptr_t)Module->NtHeaders->OptionalHeader.ImageBase & 0xffffffff00000000);
#else
	static const uintptr_t ImageOffset = 0;
#endif
	//���ļ��е�����ת��Ϊ�ڴ��е�����
	SECTIONFINALIZEDATA SectionData;
	SectionData.lpAddress = (LPVOID)((uintptr_t)Section_Header->Misc.PhysicalAddress | ImageOffset);
	SectionData.lpAlignedAddress = AlignAddressDown(SectionData.lpAddress, Module->dwPageSize);
	SectionData.Size = GetRealSectionSize(Module, Section_Header);
	SectionData.dwCharacteristics = Section_Header->Characteristics;
	SectionData.bIsLast = FALSE;
	Section_Header++;

	// ���θ��ĸ�����������
	for (i = 1; i<Module->NtHeaders->FileHeader.NumberOfSections; i++, Section_Header++)
	{
		LPVOID SectionAddress = (LPVOID)((uintptr_t)Section_Header->Misc.PhysicalAddress | ImageOffset); //���ļ�RVAתΪ�ڴ�RVA
		LPVOID AlignedAddress = AlignAddressDown(SectionAddress, Module->dwPageSize);                    //�������������ȸ�Ϊϵͳ�ڴ�ҳ��С
		SIZE_T SectionSize = GetRealSectionSize(Module, Section_Header);                              //���ļ��н�����СתΪ�ڴ��н�����С


		if (SectionData.lpAlignedAddress == AlignedAddress || (uintptr_t)SectionData.lpAddress + SectionData.Size >(uintptr_t) AlignedAddress)
		{
			// �ڵ������ڽ��������󽫶���
			if ((Section_Header->Characteristics & IMAGE_SCN_MEM_DISCARDABLE) == 0 || (SectionData.dwCharacteristics & IMAGE_SCN_MEM_DISCARDABLE) == 0)
			{
				SectionData.dwCharacteristics = (SectionData.dwCharacteristics | Section_Header->Characteristics) & ~IMAGE_SCN_MEM_DISCARDABLE;
			}
			else
			{
				SectionData.dwCharacteristics |= Section_Header->Characteristics;
			}
			SectionData.Size = (((uintptr_t)SectionAddress) + ((uintptr_t)SectionSize)) - (uintptr_t)SectionData.lpAddress;
			continue;
		}

		if (!FinalizeSection(Module, &SectionData))
		{
			return FALSE;
		}
		SectionData.lpAddress = SectionAddress;
		SectionData.lpAlignedAddress = AlignedAddress;
		SectionData.Size = SectionSize;
		SectionData.dwCharacteristics = Section_Header->Characteristics;
	}
	SectionData.bIsLast = TRUE;
	if (!FinalizeSection(Module, &SectionData))
	{
		return FALSE;
	}
	return TRUE;
}

//ִ��TLS�ص�����
static BOOL ExecuteTLS(PMEMORYMODULE Module)
{
	unsigned char *CodeBase = Module->pCodeBase;
	PIMAGE_TLS_DIRECTORY TLSDirectory;
	PIMAGE_TLS_CALLBACK* CallBack;

	PIMAGE_DATA_DIRECTORY Directory = GET_HEADER_DICTIONARY(Module, IMAGE_DIRECTORY_ENTRY_TLS);
	if (Directory->VirtualAddress == 0)
	{
		return TRUE;
	}

	TLSDirectory = (PIMAGE_TLS_DIRECTORY)(CodeBase + Directory->VirtualAddress);
	CallBack = (PIMAGE_TLS_CALLBACK *)TLSDirectory->AddressOfCallBacks;
	if (CallBack)
	{
		while (*CallBack)
		{
			//�����̿�ʼʱִ��
			(*CallBack)((LPVOID)CodeBase, DLL_PROCESS_ATTACH, NULL);
			CallBack++;
		}
	}
	return TRUE;
}



//�Զ���LoadLirbrary
HMEMORYMODULE MemoryLoadLibrary(const void *Data, size_t Size)
{//#������malloc���ڴ���ʼ��ַ��0
	return MemoryLoadLibraryEx(Data, Size, MemoryDefaultAlloc, MemoryDefaultFree, MemoryDefaultLoadLibrary, MemoryDefaultGetProcAddress, MemoryDefaultFreeLibrary, NULL);
}

HMEMORYMODULE MemoryLoadLibraryEx(const void *Data, size_t Size,
	pfnAlloc MyAllocMemory,
	pfnFree MyFreeMemory,
	pfnLoadLibrary MyLoadLibrary,
	pfnGetProcAddress MyGetProcAddress,
	pfnFreeLibrary MyFreeLibrary,
	void *UserData)
{
//#������malloc���ڴ���ʼ��ַ��0
	PMEMORYMODULE hModule = NULL;
	PIMAGE_DOS_HEADER Dos_Header;     //���ص�ַ��
	PIMAGE_NT_HEADERS Nt_Header;      //ԭ����Ntͷ
	unsigned char *Code, *Headers;
	ptrdiff_t LocationDelta;
	SYSTEM_INFO SystemInfo;
	PIMAGE_SECTION_HEADER Section_Header;
	DWORD i;
	size_t OptionalSectionSize;
	size_t LastSectionEnd = 0;
	size_t AlignedImageSize;

	if (!CheckSize(Size, sizeof(IMAGE_DOS_HEADER)))
	{//#���ڵ��ڷ���true���ļ���С������dosͷ��ô��
		return NULL;
	}
	Dos_Header = (PIMAGE_DOS_HEADER)Data;
	if (Dos_Header->e_magic != IMAGE_DOS_SIGNATURE)   //MZ
	{
		SetLastError(ERROR_BAD_EXE_FORMAT);
		return NULL;
	}

	if (!CheckSize(Size, Dos_Header->e_lfanew + sizeof(IMAGE_NT_HEADERS)))
	{//#dll�ļ����ٰ���Ntͷ��ô��
		return NULL;
	}

	Nt_Header = (PIMAGE_NT_HEADERS)&((const unsigned char *)(Data))[Dos_Header->e_lfanew];
	if (Nt_Header->Signature != IMAGE_NT_SIGNATURE) //PE00
	{
		SetLastError(ERROR_BAD_EXE_FORMAT);
		return NULL;
	}

	if (Nt_Header->FileHeader.Machine != HOST_MACHINE)
	{
		SetLastError(ERROR_BAD_EXE_FORMAT);
		return NULL;
	}

	if (Nt_Header->OptionalHeader.SectionAlignment & 1)
	{
		// ����������С����2
		SetLastError(ERROR_BAD_EXE_FORMAT);
		return NULL;
	}
	//#��2��������һϵ��dll�ļ���Ϣ�ж�
	Section_Header = IMAGE_FIRST_SECTION(Nt_Header);
	OptionalSectionSize = Nt_Header->OptionalHeader.SectionAlignment;  //������������ ���ڴ��У�
	for (i = 0; i<Nt_Header->FileHeader.NumberOfSections; i++, Section_Header++)
	{//#�õ������lastsection��end
		size_t EndOfSection;
		if (Section_Header->SizeOfRawData == 0)
		{
			// һ���սڣ�ֱ���Զ������ȶ���
			EndOfSection = Section_Header->VirtualAddress + OptionalSectionSize;
		}
		else
		{
			EndOfSection = Section_Header->VirtualAddress + Section_Header->SizeOfRawData;//����RVA + ���ļ��ж����ĳߴ�
		}

		if (EndOfSection >  LastSectionEnd)
		{
			LastSectionEnd = EndOfSection;
		}
	}

	GetNativeSystemInfo(&SystemInfo);
	AlignedImageSize = AlignValueUp(Nt_Header->OptionalHeader.SizeOfImage, SystemInfo.dwPageSize);

	if (AlignedImageSize != AlignValueUp(LastSectionEnd, SystemInfo.dwPageSize))
	{
		SetLastError(ERROR_BAD_EXE_FORMAT);
		return NULL;
	}

	//��Dll������ػ���ַ�������ڴ�ռ�
	Code = (unsigned char *)MyAllocMemory((LPVOID)(Nt_Header->OptionalHeader.ImageBase),
		AlignedImageSize,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE,
		UserData);

	if (Code == NULL)
	{
		//������ɹ����������Ҹ��ض�����
		Code = (unsigned char *)MyAllocMemory(NULL,
			AlignedImageSize,
			MEM_RESERVE | MEM_COMMIT,
			PAGE_READWRITE,
			UserData);
		if (Code == NULL)
		{
			SetLastError(ERROR_OUTOFMEMORY);
			return NULL;
		}
	}

	hModule = (PMEMORYMODULE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MEMORYMODULE));
	if (hModule == NULL)
	{
		MyFreeMemory(Code, 0, MEM_RELEASE, UserData);
		SetLastError(ERROR_OUTOFMEMORY);
		return NULL;
	}

	hModule->pCodeBase = Code;
	hModule->bIsDLL = (Nt_Header->FileHeader.Characteristics & IMAGE_FILE_DLL) != 0;
	hModule->MyAlloc = MyAllocMemory;
	hModule->MyFree = MyFreeMemory;
	hModule->MyLoadLibrary = MyLoadLibrary;
	hModule->MyGetProcAddress = MyGetProcAddress;
	hModule->MyFreeLibrary = MyFreeLibrary;
	hModule->pUserData = UserData;
	hModule->dwPageSize = SystemInfo.dwPageSize;

	if (!CheckSize(Size, Nt_Header->OptionalHeader.SizeOfHeaders))
	{
		goto error;
	}

	//����ͷ���ռ�
	Headers = (unsigned char *)MyAllocMemory(Code,
		Nt_Header->OptionalHeader.SizeOfHeaders,
		MEM_COMMIT,
		PAGE_READWRITE,
		UserData);

	//��Dll�е�Ntͷ������ָ�������ڴ洦
	memcpy(Headers, Dos_Header, Nt_Header->OptionalHeader.SizeOfHeaders);
	hModule->NtHeaders = (PIMAGE_NT_HEADERS)&((const unsigned char *)(Headers))[Dos_Header->e_lfanew];

	// ����Dll���ػ���ַ
#ifdef _WIN64
	hModule->NtHeaders->OptionalHeader.ImageBase = (DWORD64)Code;
#else
	hModule->NtHeaders->OptionalHeader.ImageBase = (DWORD)Code;
#endif // _WIN64	

	//������
	if (!CopySections((const unsigned char *)Data, Size, Nt_Header, hModule))
	{
		goto error;
	}

	// �����ض�������λ��
	LocationDelta = (ptrdiff_t)(hModule->NtHeaders->OptionalHeader.ImageBase - Nt_Header->OptionalHeader.ImageBase);
	if (LocationDelta != 0)
	{
		hModule->bIsRelocated = PerformBaseRelocation(hModule, LocationDelta);
	}
	else
	{
		hModule->bIsRelocated = TRUE;
	}

	// ���������
	if (!BuildImportTable(hModule))
	{
		goto error;
	}

	//�����ڵ�ĳЩ����
	if (!FinalizeSections(hModule))
	{
		goto error;
	}

	// ִ��TLS�ص�������TLS�ص������ڳ���EntryPoint֮ǰִ��
	if (!ExecuteTLS(hModule))
	{
		goto error;
	}

	//���Dll����ִ����ڵ�ַ
	if (hModule->NtHeaders->OptionalHeader.AddressOfEntryPoint != 0)
	{
		if (hModule->bIsDLL)
		{
			//����ִ�����
			DllEntryProc DllEntry = (DllEntryProc)(LPVOID)(Code + hModule->NtHeaders->OptionalHeader.AddressOfEntryPoint);

			BOOL bOk = DllEntry((HMODULE)Code, DLL_PROCESS_ATTACH, 0);
			//BOOL bOk = (*DllEntry)((HINSTANCE)Code, DLL_PROCESS_ATTACH, 0);
			if (!bOk)
			{
				SetLastError(ERROR_DLL_INIT_FAILED);
				goto error;
			}
			hModule->bInitialized = TRUE;
		}
		else
		{
			hModule->ExeEntry = (ExeEntryProc)(LPVOID)(Code + hModule->NtHeaders->OptionalHeader.AddressOfEntryPoint);
		}
	}
	else
	{
		hModule->ExeEntry = NULL;
	}

	return (HMEMORYMODULE)hModule;

error:
	// �����ڴ�
	MemoryFreeLibrary(hModule);
	return NULL;
}

//�ͷŶ�̬��
void MemoryFreeLibrary(HMEMORYMODULE Module)
{
	PMEMORYMODULE pModule = (PMEMORYMODULE)Module;

	if (pModule == NULL)
	{
		return;
	}
	if (pModule->bInitialized)
	{
		//֪ͨDll DLL_PROCESS_DETACH
		DllEntryProc DllEntry = (DllEntryProc)(LPVOID)(pModule->pCodeBase + pModule->NtHeaders->OptionalHeader.AddressOfEntryPoint);
		(*DllEntry)((HINSTANCE)pModule->pCodeBase, DLL_PROCESS_DETACH, 0);
	}

	if (pModule->pModules != NULL)
	{
		// 
		int i;
		for (i = 0; i<pModule->nNumberOfModules; i++)
		{
			if (pModule->pModules[i] != NULL)
			{
				pModule->MyFreeLibrary(pModule->pModules[i], pModule->pUserData);
			}
		}

		free(pModule->pModules);
	}

	if (pModule->pCodeBase != NULL)
	{
		//�ͷ��ڴ�
		pModule->MyFree(pModule->pCodeBase, 0, MEM_RELEASE, pModule->pUserData);
	}

	HeapFree(GetProcessHeap(), 0, pModule);
}

//GetProcAddress
FARPROC MemoryGetProcAddress(HMEMORYMODULE Module, LPCSTR lpProcName)
{
	unsigned char *CodeBase = ((PMEMORYMODULE)Module)->pCodeBase;
	DWORD  dwIndex = 0;
	PIMAGE_EXPORT_DIRECTORY ExportDirectory;
	PIMAGE_DATA_DIRECTORY Directory = GET_HEADER_DICTIONARY((PMEMORYMODULE)Module, IMAGE_DIRECTORY_ENTRY_EXPORT);
	if (Directory->Size == 0)
	{
		// �޵�����
		SetLastError(ERROR_PROC_NOT_FOUND);
		return NULL;
	}

	ExportDirectory = (PIMAGE_EXPORT_DIRECTORY)(CodeBase + Directory->VirtualAddress);
	if (ExportDirectory->NumberOfNames == 0 || ExportDirectory->NumberOfFunctions == 0)
	{
		// Dllδ�����κκ���
		SetLastError(ERROR_PROC_NOT_FOUND);
		return NULL;
	}

	if (HIWORD(lpProcName) == 0)
	{
		// ����ŵ���
		if (LOWORD(lpProcName) < ExportDirectory->Base)
		{
			SetLastError(ERROR_PROC_NOT_FOUND);
			return NULL;
		}

		dwIndex = LOWORD(lpProcName) - ExportDirectory->Base;
	}
	else
	{
		//�ڵ�������Ѱ��Ŀ�꺯��
		DWORD i;
		DWORD *NameRef = (DWORD *)(CodeBase + ExportDirectory->AddressOfNames);
		WORD  *Ordinal = (WORD *)(CodeBase + ExportDirectory->AddressOfNameOrdinals);
		BOOL  bFound = FALSE;
		for (i = 0; i<ExportDirectory->NumberOfNames; i++, NameRef++, Ordinal++)
		{
			if (_stricmp(lpProcName, (const char *)(CodeBase + (*NameRef))) == 0)
			{
				dwIndex = *Ordinal;
				bFound = TRUE;
				break;
			}
		}

		if (!bFound)
		{
			// δ�ҵ�����
			SetLastError(ERROR_PROC_NOT_FOUND);
			return NULL;
		}
	}

	if (dwIndex > ExportDirectory->NumberOfFunctions)
	{

		SetLastError(ERROR_PROC_NOT_FOUND);
		return NULL;
	}

	// ��RVA��ΪVA
	return (FARPROC)(LPVOID)(CodeBase + (*(DWORD *)(CodeBase + ExportDirectory->AddressOfFunctions + (dwIndex * 4))));
}