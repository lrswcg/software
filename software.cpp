// Shell.cpp : ���� DLL Ӧ�ó���ĵ���������
//

#include "stdafx.h"
#include "Shell.h"

#pragma comment(linker, "/merge:.data=.text") 
#pragma comment(linker, "/merge:.rdata=.text")
#pragma comment(linker, "/section:.text,RWE")

//�����ͱ���������
DWORD MyGetProcAddress();		//�Զ���GetProcAddress
HMODULE	GetKernel32Addr();		//��ȡKernel32���ػ�ַ
void Start();					//��������(Shell���ֵ���ں���)
void InitFun();					//��ʼ������ָ��ͱ���
void DeXorCode();				//���ܲ���
void RecReloc();				//�޸��ض�λ����
void RecIAT();					//�޸�IAT����
SHELL_DATA g_stcShellData = { (DWORD)Start };
								//Shell�õ���ȫ�ֱ����ṹ��
DWORD dwImageBase	= 0;		//��������ľ����ַ
DWORD dwPEOEP		= 0;		//PE�ļ���OEP

//Shell�����õ��ĺ�������
fnGetProcAddress	g_pfnGetProcAddress		= NULL;
fnLoadLibraryA		g_pfnLoadLibraryA		= NULL;
fnGetModuleHandleA	g_pfnGetModuleHandleA	= NULL;
fnVirtualProtect	g_pfnVirtualProtect		= NULL;
fnVirtualAlloc		g_pfnVirtualAlloc		= NULL;
fnExitProcess		g_pfnExitProcess		= NULL;
fnMessageBox		g_pfnMessageBoxA		= NULL;

 //************************************************************
// ��������:	Start
// ����˵��:	��������(Shell���ֵ���ں���)
// �� ��	ֵ:	void
//************************************************************
__declspec(naked) void Start()
{
 	__asm pushad

	InitFun();

	DeXorCode();

	if (g_stcShellData.stcPERelocDir.VirtualAddress)
	{
		RecReloc();
	}
	RecIAT();

	if (g_stcShellData.bIsShowMesBox)
	{
		g_pfnMessageBoxA(0, "Hello World!", "Hello!", 0);
	}

	__asm popad

	//��ȡOEP��Ϣ
	dwPEOEP = g_stcShellData.dwPEOEP + dwImageBase;
	__asm jmp dwPEOEP
	
	g_pfnExitProcess(0);	//ʵ�ʲ���ִ�д���ָ��
}

//************************************************************
// ��������:	RecIAT
// ����˵��:	�޸�IAT����
// �� ��	ֵ:	void
//************************************************************
void RecIAT()
{
	//1.��ȡ�����ṹ��ָ��
	PIMAGE_IMPORT_DESCRIPTOR pPEImport = 
		(PIMAGE_IMPORT_DESCRIPTOR)(dwImageBase + g_stcShellData.stcPEImportDir.VirtualAddress);
	
	//2.�޸��ڴ�����Ϊ��д
	DWORD dwOldProtect = 0;
	g_pfnVirtualProtect(
		(LPBYTE)(dwImageBase + g_stcShellData.dwIATSectionBase), g_stcShellData.dwIATSectionSize,
		PAGE_EXECUTE_READWRITE, &dwOldProtect);

	//3.��ʼ�޸�IAT
	while (pPEImport->Name)
	{
		//��ȡģ����
		DWORD dwModNameRVA = pPEImport->Name;
		char* pModName = (char*)(dwImageBase + dwModNameRVA);
		HMODULE hMod = g_pfnLoadLibraryA(pModName);

		//��ȡIAT��Ϣ(��ЩPE�ļ�INT�ǿյģ������IAT������Ҳ���������������Ա�)
		PIMAGE_THUNK_DATA pIAT = (PIMAGE_THUNK_DATA)(dwImageBase + pPEImport->FirstThunk);
		
		//��ȡINT��Ϣ(ͬIATһ�����ɽ�INT������IAT��һ������)
		//PIMAGE_THUNK_DATA pINT = (PIMAGE_THUNK_DATA)(dwImageBase + pPEImport->OriginalFirstThunk);

		//ͨ��IATѭ����ȡ��ģ���µ����к�����Ϣ(����֮��ȡ�˺�����)
		while (pIAT->u1.AddressOfData)
		{
			//�ж�������������������
			if (IMAGE_SNAP_BY_ORDINAL(pIAT->u1.Ordinal))
			{
				//������
				DWORD dwFunOrdinal = (pIAT->u1.Ordinal) & 0x7FFFFFFF;
				DWORD dwFunAddr = g_pfnGetProcAddress(hMod, (char*)dwFunOrdinal);
				*(DWORD*)pIAT = (DWORD)dwFunAddr;
			}
			else
			{
				//���������
				DWORD dwFunNameRVA = pIAT->u1.AddressOfData;
				PIMAGE_IMPORT_BY_NAME pstcFunName = (PIMAGE_IMPORT_BY_NAME)(dwImageBase + dwFunNameRVA);
				DWORD dwFunAddr = g_pfnGetProcAddress(hMod, pstcFunName->Name);
				*(DWORD*)pIAT = (DWORD)dwFunAddr;
			}
			pIAT++;
		}
		//������һ��ģ��
		pPEImport++;
	}

	//4.�ָ��ڴ�����
	g_pfnVirtualProtect(
		(LPBYTE)(dwImageBase + g_stcShellData.dwIATSectionBase), g_stcShellData.dwIATSectionSize,
		dwOldProtect, &dwOldProtect);
}

//************************************************************
// ��������:	RecReloc
// ����˵��:	�޸��ض�λ����
// �� ��	ֵ:	void
//************************************************************
void RecReloc()
{
	typedef struct _TYPEOFFSET
	{
		WORD offset : 12;		//ƫ��ֵ
		WORD Type : 4;			//�ض�λ����(��ʽ)
	}TYPEOFFSET, *PTYPEOFFSET;

	//1.��ȡ�ض�λ��ṹ��ָ��
	PIMAGE_BASE_RELOCATION	pPEReloc=
		(PIMAGE_BASE_RELOCATION)(dwImageBase + g_stcShellData.stcPERelocDir.VirtualAddress);
	
	//2.��ʼ�޸��ض�λ
	while (pPEReloc->VirtualAddress)
	{
		//2.1�޸��ڴ�����Ϊ��д
		DWORD dwOldProtect = 0;
		g_pfnVirtualProtect((PBYTE)dwImageBase + pPEReloc->VirtualAddress,
			0x1000, PAGE_EXECUTE_READWRITE, &dwOldProtect);

		//2.2�޸��ض�λ
		PTYPEOFFSET pTypeOffset = (PTYPEOFFSET)(pPEReloc + 1);
		DWORD dwNumber = (pPEReloc->SizeOfBlock - 8) / 2;
		for (DWORD i = 0; i < dwNumber; i++)
		{
			if (*(PWORD)(&pTypeOffset[i]) == NULL)
				break;
			//RVA
			DWORD dwRVA = pTypeOffset[i].offset + pPEReloc->VirtualAddress;
			//FAR��ַ
			DWORD AddrOfNeedReloc = *(PDWORD)((DWORD)dwImageBase + dwRVA);
			*(PDWORD)((DWORD)dwImageBase + dwRVA) = 
				AddrOfNeedReloc - g_stcShellData.dwPEImageBase + dwImageBase;
		}

		//2.3�ָ��ڴ�����
		g_pfnVirtualProtect((PBYTE)dwImageBase + pPEReloc->VirtualAddress,
			0x1000, dwOldProtect, &dwOldProtect);

		//2.4�޸���һ������
		pPEReloc = (PIMAGE_BASE_RELOCATION)((DWORD)pPEReloc + pPEReloc->SizeOfBlock);
	}
}


//************************************************************
// ��������:	DeXorCode
// ����˵��:	���ܲ���
// �� ��	ֵ:	void
//************************************************************
void DeXorCode()
{
	PBYTE pCodeBase = (PBYTE)g_stcShellData.dwCodeBase + dwImageBase;

	DWORD dwOldProtect = 0;
	g_pfnVirtualProtect(pCodeBase, g_stcShellData.dwXorSize, PAGE_EXECUTE_READWRITE, &dwOldProtect);

	for (DWORD i = 0; i < g_stcShellData.dwXorSize; i++)
	{
		pCodeBase[i] ^= i;
	}

	g_pfnVirtualProtect(pCodeBase, g_stcShellData.dwXorSize, dwOldProtect, &dwOldProtect);
}

//************************************************************
// ��������:	InitFun
// ����˵��:	��ʼ������ָ��ͱ���
// ��	��:	cyxvc
// ʱ	��:	2015/12/28
// �� ��	ֵ:	void
//************************************************************
void InitFun()
{
	//��Kenel32�л�ȡ����
	HMODULE hKernel32		= GetKernel32Addr();
	g_pfnGetProcAddress		= (fnGetProcAddress)MyGetProcAddress();
	g_pfnLoadLibraryA		= (fnLoadLibraryA)g_pfnGetProcAddress(hKernel32, "LoadLibraryA");
	g_pfnGetModuleHandleA	= (fnGetModuleHandleA)g_pfnGetProcAddress(hKernel32, "GetModuleHandleA");
	g_pfnVirtualProtect		= (fnVirtualProtect)g_pfnGetProcAddress(hKernel32, "VirtualProtect");
	g_pfnExitProcess		= (fnExitProcess)g_pfnGetProcAddress(hKernel32, "ExitProcess");
	g_pfnVirtualAlloc		= (fnVirtualAlloc)g_pfnGetProcAddress(hKernel32, "VirtualAlloc");

	//��user32�л�ȡ����
	HMODULE hUser32			= g_pfnLoadLibraryA("user32.dll");
	g_pfnMessageBoxA		= (fnMessageBox)g_pfnGetProcAddress(hUser32, "MessageBoxA");

	//��ʼ�������ַ
	dwImageBase =			(DWORD)g_pfnGetModuleHandleA(NULL);
}


//************************************************************
// ��������:	GetKernel32Addr
// ����˵��:	��ȡKernel32���ػ�ַ
// �� ��	ֵ:	HMODULE
//************************************************************
HMODULE GetKernel32Addr()
{
	HMODULE dwKernel32Addr = 0;
	__asm
	{
		push eax
			mov eax, dword ptr fs : [0x30]   // eax = PEB�ĵ�ַ
			mov eax, [eax + 0x0C]            // eax = ָ��PEB_LDR_DATA�ṹ��ָ��
			mov eax, [eax + 0x1C]            // eax = ģ���ʼ�������ͷָ��InInitializationOrderModuleList
			mov eax, [eax]                   // eax = �б��еĵڶ�����Ŀ
			mov eax, [eax]                   // eax = �б��еĵ�������Ŀ
			mov eax, [eax + 0x08]            // eax = ��ȡ����Kernel32.dll��ַ(Win7�µ�������Ŀ��Kernel32.dll)
			mov dwKernel32Addr, eax
			pop eax
	}
	return dwKernel32Addr;
}


//************************************************************
// ��������:	MyGetProcAddress
// ����˵��:	�Զ���GetProcAddress
// �� ��	ֵ:	DWORD
//************************************************************
DWORD MyGetProcAddress()
{
	HMODULE hModule = GetKernel32Addr();

	//1.��ȡDOSͷ
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)(PBYTE)hModule;
	//2.��ȡNTͷ
	PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)hModule + pDosHeader->e_lfanew);
	//3.��ȡ������Ľṹ��ָ��
	PIMAGE_DATA_DIRECTORY pExportDir =
		&(pNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]);

	PIMAGE_EXPORT_DIRECTORY pExport = 
		(PIMAGE_EXPORT_DIRECTORY)((PBYTE)hModule + pExportDir->VirtualAddress);

	//EAT
	PDWORD pEAT = (PDWORD)((DWORD)hModule + pExport->AddressOfFunctions);
	//ENT
	PDWORD pENT = (PDWORD)((DWORD)hModule + pExport->AddressOfNames);
	//EIT
	PWORD pEIT = (PWORD)((DWORD)hModule + pExport->AddressOfNameOrdinals);

	//4.������������ȡGetProcAddress()������ַ
	DWORD dwNumofFun = pExport->NumberOfFunctions;
	DWORD dwNumofName = pExport->NumberOfNames;
	for (DWORD i = 0; i < dwNumofFun; i++)
	{
		//���Ϊ��Ч����������
		if (pEAT[i] == NULL)
			continue;
		//�ж����Ժ�����������������ŵ���
		DWORD j = 0;
		for (; j < dwNumofName; j++)
		{
			if (i == pEIT[j])
			{
				break;
			}
		}
		if (j != dwNumofName)
		{
			//����Ǻ�������ʽ������
			//������
			char* ExpFunName = (CHAR*)((PBYTE)hModule + pENT[j]);
			//���жԱ�,�����ȷ���ص�ַ
			if (!strcmp(ExpFunName, "GetProcAddress"))
			{
				return pEAT[i] + pNtHeader->OptionalHeader.ImageBase;
			}
		}
		else
		{
			//���
		}
	}
	return 0;
}