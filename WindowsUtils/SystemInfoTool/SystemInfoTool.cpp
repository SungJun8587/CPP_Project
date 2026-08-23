//***************************************************************************
// SystemInfoTool.cpp : Defines the entry point for the console application.
//
//***************************************************************************

#include <pch.h>

#include <iostream>
#include <vector>
#include <bitset>
#include <array>
#include <string>
#include <intrin.h>

#define VS_SEVICE_TITLE _T("winmgmt")

// ClearConsoleScreen()/PauseConsole()/InitUtf8Console()은 Util/ConsoleUtil.h
// (pch.h가 include)에 정의되어 있습니다. 이 파일에서 직접 정의하지 않는 이유는
// 다음과 같습니다.
//   1. 재사용성
//      1-1. 이 세 함수는 콘솔을 다루는 다른 도구(Wmi 기반 툴 등)에서도
//           똑같이 필요할 가능성이 높은 범용 유틸리티입니다.
//      1-2. 이 파일 안에 static으로 가둬두면 다른 .cpp에서 동일한 코드를
//           또 작성해야 해서 중복이 발생합니다.
//   2. 매크로 대신 함수로 통일
//      2-1. InitUtf8Console()도 ClearConsoleScreen()/PauseConsole()과 동일하게
//           함수로 통일했습니다(자세한 이유는 Util/ConsoleUtil.h 상단 주석 참고).
//      2-2. <conio.h> include도 ConsoleUtil.h에 있어 이 파일에서는 필요 없습니다.
//
// Sound/Video는 WMI 버전(CWmiSoundCardInfo/CWmiVideoCardInfo, Win32_SoundDevice/
// Win32_VideoController)을 씁니다 - ChipType/DacType/AdapterString/DisplayDrivers/
// 볼륨제어 플래그까지 실제로 채워줍니다. CPU/PCI는 두 버전 다 특성이 달라(CPUID
// 명령어 세트 지원 여부 vs 코어/캐시 정보, Bus/Device/Function 정밀도 vs WMI 부재)
// 상호 대체가 안 되므로 둘 다 출력합니다.

//***************************************************************************
//
int main(int argc, char* argv[])
{
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_LEAK_CHECK_DF);
#endif

	InitUtf8Console();

	TCHAR	tszFormat[NUMERIC_STRING_LEN];

	CCpuInfo				CpuInfo;
	CWmiBiosInfo			BiosInfo;
	CWmiMainBoardInfo		MainBoardInfo;
	CWmiMemoryInfo			MemoryInfo;
	CWmiHdDiskInfo			HdDiskInfo;
	CWmiDriveInfo			DriveInfo;
	CWmiSoundCardInfo		SoundCardInfo;
	CWmiVideoCardInfo		VideoCardInfo;
	CWmiNetworkCardInfo		NetworkCardInfo;
	CPciInfo				PciInfo;
	CWmiPciInfo				WmiPciInfo;
	CWmiCdromInfo			CdromInfo;
	CWmiKeyBoardInfo		KeyBoardInfo;
	CWmiMouseInfo			MouseInfo;
	CWmiMonitorInfo			MonitorInfo;

	COsInfo			OsInfo;
	CIeInfo			IeInfo;
	CDirectXInfo	DirectXInfo;
	CJavaVMInfo		JavaVMInfo;
	CInstallSwInfo	InstallSwInfo;

	// 이 블록은 기존 winmgmt 서비스의 시작 유형만 바꾸는 것이라 필요한 권한은
	// SC_MANAGER_CONNECT면 충분합니다. hScm/hService는 실패 시 NULL을 반환하므로
	// 사용 전에 유효성을 체크합니다.
	SC_HANDLE	hScm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	if( hScm )
	{
		SC_HANDLE hService = OpenService(hScm, VS_SEVICE_TITLE, SC_MANAGER_ALL_ACCESS);
		if( hService )
		{
			ChangeServiceConfig(hService, SERVICE_NO_CHANGE, SERVICE_AUTO_START, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
			CloseServiceHandle(hService);
		}
		CloseServiceHandle(hScm);
	}

	// EXE가 위치한 디렉터리를 구해 그 아래에 "Log\" 경로를 만듭니다.
	// [주의] CLog::Write()는 대상 디렉터리를 자동 생성하지 않고, fopen이 실패하면
	// (fp == 0x00) 아무 에러 표시 없이 그냥 리턴합니다 — 즉 폴더가 없으면 로그가
	// "조용히" 전부 유실됩니다. 그래서 CreateDirectory()를 반드시 먼저 호출해야 합니다.
	TCHAR tszExePath[MAX_PATH] = { 0, };
	DWORD dwLen = GetModuleFileName(NULL, tszExePath, MAX_PATH);
	if( dwLen == 0 || dwLen == MAX_PATH )
	{
		// 조회 실패 또는 경로가 MAX_PATH를 넘어 잘린 경우: 현재 작업 디렉터리로 대체
		_tcscpy_s(tszExePath, MAX_PATH, _T(".\\"));
	}

	_tstring strExePath = tszExePath;
	size_t nPos = strExePath.find_last_of(_T("\\/"));
	_tstring strExeDir = (nPos != _tstring::npos) ? strExePath.substr(0, nPos + 1) : _T(".\\");

	_tstring strLogPath = strExeDir + _T("Log\\");

	CreateDirectory(strLogPath.c_str(), NULL);

	// CLog::_tszDirectory가 TCHAR[DIRECTORY_STRLEN](=256) 고정 버퍼이고 내부에서
	// _tcsncpy_s(..., _TRUNCATE)로 안전하게 잘라 복사하므로, 여기서 c_str()를 그대로
	// 넘겨도 오버플로우는 없습니다. 다만 strLogPath가 256자를 넘으면 잘린 채로
	// 저장되어 의도한 경로와 달라질 수 있습니다(경로가 극단적으로 깊은 경우에만 해당).
	CLogManager::Instance().Create(strLogPath.c_str());

	// COM 라이브러리 초기화 - 이 스레드에서 사용하는 모든 COM/WMI 리소스는
	// 여기서부터 아래쪽 CoUninitialize() 호출 전까지의 구간에서만 유효합니다.
	HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if( FAILED(hrCom) )
	{
		LOG_ERROR(_T("CoInitializeEx Failed. HRESULT: 0x%08X"), hrCom);
		return -1;
	}

	HRESULT hr = CoInitializeSecurity(
		NULL,
		-1,                          // COM authentication
		NULL,                        // Authentication services
		NULL,                        // Reserved
		RPC_C_AUTHN_LEVEL_DEFAULT,   // Default authentication 
		RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation  
		NULL,                        // Authentication info
		EOAC_NONE,                   // Additional capabilities 
		NULL                         // Reserved
	);

	if( FAILED(hr) )
	{
		LOG_ERROR(_T("CoInitializeSecurity Failed. HRESULT: 0x%08X"), hr);
		CoUninitialize();
		return -1;
	}

	// CWmi는 이 중첩 스코프 안에서만 생성/사용되며, 스코프를 벗어날 때
	// (아래쪽 CoUninitialize() 호출 전에) 소멸되어 COM이 살아있는 상태에서
	// 안전하게 인터페이스를 Release() 합니다.
	{
		CWmi Wmi;

		// CWmi 연결 및 예외 처리
		if( !Wmi.Connect() )
		{
			LOG_ERROR(_T("WMI Connection Failed."));
			CoUninitialize();
			return -1;
		}

		// 1. PROCESSOR INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*************************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* PROCESSOR INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*************************\n"));

		CpuInfo.GetInformation();

		TCHAR tszBuffer1[3] = { ' ', ' ', '\0' };

		_tstring strCPUName = CpuInfo.GetProcessorName();
		const _tstring strTarget = tszBuffer1;
		const _tstring strReplace = _T("");

		// 문자열 내 tszBuffer1 검색 및 제거(빈 문자열로 치환)
		if( !strTarget.empty() )
		{
			size_t nPos = 0;
			while( (nPos = strCPUName.find(strTarget, nPos)) != _tstring::npos )
			{
				strCPUName.replace(nPos, strTarget.length(), strReplace);
				nPos += strReplace.length();
			}
		}

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Vendor Name = %s"), CpuInfo.GetVendorName());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Processor Name = %s"), strCPUName.c_str());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Processor Id = %s"), CpuInfo.GetProcessorId());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Speed (MHz) = %d MHz"), CpuInfo.GetSpeedMHz());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Processors Count (Logical) = %d"), CpuInfo.GetNumberOfProcessors());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Processors Count (Physical Cores) = %u"), CpuInfo.GetNumberOfCores());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Signature Info = Family %d, Model %d, Stepping %d"), CpuInfo.GetCPUFamily(), CpuInfo.GetCPUModel(), CpuInfo.GetCPUStepping());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("L2CacheSize = %u KB, L3CacheSize = %u KB"), CpuInfo.GetL2CacheSize(), CpuInfo.GetL3CacheSize());

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Instruction Set Flags:"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("    * MMX = %s"), CpuInfo.IsMMXSupported() ? _T("Supported") : _T("Not Supported"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("    * SSE = %s"), CpuInfo.IsSSESupported() ? _T("Supported") : _T("Not Supported"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("    * SSE2 = %s"), CpuInfo.IsSSE2Supported() ? _T("Supported") : _T("Not Supported"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("    * 3DNow! = %s\n"), CpuInfo.Is3DNowSupported() ? _T("Supported") : _T("Not Supported"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 2. BIOS INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("********************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* BIOS INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("********************\n"));

		BiosInfo.GetInformation(Wmi);

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("BIOS Manufacturer = %s"), BiosInfo.GetManufacturer());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("BIOS SmVersion = %s"), BiosInfo.GetSmVersion());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("BIOS Version = %s"), BiosInfo.GetVersion());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("BIOS IdentificationCode = %s"), BiosInfo.GetIdentificationCode());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("BIOS SerialNumber = %s"), BiosInfo.GetSerialNumber());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("BIOS ReleaseDate = %s\n"), BiosInfo.GetReleaseDate());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 3. MAINBOARD INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*************************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* MAINBOARD INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*************************\n"));

		MainBoardInfo.GetInformation(Wmi);

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("MAINBOARD Product = %s"), MainBoardInfo.GetProduct());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("MAINBOARD SerialNumber = %s"), MainBoardInfo.GetSerialNumber());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("MAINBOARD Manufacturer = %s"), MainBoardInfo.GetManufacturer());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("MAINBOARD Description = %s\n"), MainBoardInfo.GetDescription());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 4. MEMORY INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("**********************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* MEMORY INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("**********************\n"));

		MemoryInfo.GetInformation(Wmi);
		const std::vector<HWINFO_RAM*>* psRamVector = MemoryInfo.GetRamArray();

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("--------- RAM INFORMATION ---------"));
		if( psRamVector )
		{
			for( size_t i = 0; i < psRamVector->size(); ++i )
			{
				HWINFO_RAM* pRam = (*psRamVector)[i];
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("RAM[%zu]"), i + 1);

				ChangeDataFormat(pRam->m_nCapacity, tszFormat);

				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Memory BankLabel = %s"), pRam->m_tszBankLabel);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Memory Name = %s"), pRam->m_tszName);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Memory Manufacturer = %s"), pRam->m_tszManufacturer);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Memory DeviceLocator = %s"), pRam->m_tszDeviceLocator);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Memory Size = %s"), tszFormat);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Memory FormFactor = %d[ %s ]"), pRam->m_dwFormFactor, pRam->m_tszFormFactorDesc);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Memory MemoryType = %d[ %s ]"), pRam->m_dwMemoryType, pRam->m_tszMemoryTypeDesc);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Memory Speed = %u MT/s\n"), pRam->m_dwSpeed);
			}
		}

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("-------- MEMORY INFORMATION --------"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Total RAM Count = %d"), MemoryInfo.GetRamCount());

		ChangeDataFormat(MemoryInfo.GetTotalMemSize(), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Total Memory Size = %s"), tszFormat);

		ChangeDataFormat(MemoryInfo.GetPhysicalMemSize(), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Physical Memory Size = %s"), tszFormat);

		ChangeDataFormat(MemoryInfo.GetUseMemSize(), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Used Memory Size = %s"), tszFormat);

		ChangeDataFormat(MemoryInfo.GetTotalVirtualMemSize(), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Total Virtual Memory Size = %s"), tszFormat);

		ChangeDataFormat(MemoryInfo.GetFreeVirtualMemSize(), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Free Virtual Memory Size = %s"), tszFormat);

		ChangeDataFormat(MemoryInfo.GetTotalPageFile(), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Total PageFile Size = %s"), tszFormat);

		ChangeDataFormat(MemoryInfo.GetFreePageFile(), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Free PageFile Size = %s\n"), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 5. DRIVES INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("**********************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* DRIVES INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("**********************\n"));

		HdDiskInfo.GetInformation(Wmi);
		const std::vector<HWINFO_HDDISK*>* psHdDiskVector = HdDiskInfo.GetHdDiskArray();

		if( psHdDiskVector )
		{
			for( size_t i = 0; i < psHdDiskVector->size(); ++i )
			{
				HWINFO_HDDISK* pHdDisk = (*psHdDiskVector)[i];
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("HDDISK[%zu]"), i + 1);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] HdDisk Model = %s"), pHdDisk->m_tszModel);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] HdDisk Name = %s"), pHdDisk->m_tszName);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] HdDisk Manufacturer = %s"), pHdDisk->m_tszManufacturer);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] HdDisk Description = %s"), pHdDisk->m_tszDescription);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] HdDisk SerialNumber = %s"), pHdDisk->m_tszSerialNumber);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] HdDisk BusType = %s"), pHdDisk->m_tszBusType);

				ChangeDataFormat(pHdDisk->m_nTotalSize, tszFormat);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] HdDisk TotalSize = %s\n"), tszFormat);
			}
		}
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 6. LOGICAL DISK INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("****************************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* LOGICAL DISK INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("****************************\n"));

		DriveInfo.GetInformation(Wmi);
		const std::vector<HWINFO_DRIVE*>* psDriveVector = DriveInfo.GetDriveArray();

		if( psDriveVector )
		{
			for( size_t i = 0; i < psDriveVector->size(); ++i )
			{
				HWINFO_DRIVE* pDrive = (*psDriveVector)[i];
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("DRIVE[%zu]"), i + 1);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Drive Name = %s"), pDrive->m_tszName);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Drive FileSystem = %s"), pDrive->m_tszFileSystem);

				ChangeDataFormat(pDrive->m_nTotalSpace, tszFormat);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Drive TotalSpace = %s"), tszFormat);

				ChangeDataFormat(pDrive->m_nFreeSpace, tszFormat);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Drive FreeSpace = %s"), tszFormat);

				ChangeDataFormat(pDrive->m_nTotalSpace - pDrive->m_nFreeSpace, tszFormat);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Drive UsedSpace = %s\n"), tszFormat);
			}
		}

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("-------- DRIVE INFORMATION --------"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Total Drive Count = %d"), DriveInfo.GetDriveCount());

		ChangeDataFormat(DriveInfo.GetTotalSpaceSize(), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Total Space Size = %s"), tszFormat);

		ChangeDataFormat(DriveInfo.GetFreeSpaceSize(), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Free Space Size = %s"), tszFormat);

		ChangeDataFormat(DriveInfo.GetUsedSpaceSize(), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Used Space Size = %s\n"), tszFormat);
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 7. SOUNDCARD INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*************************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* SOUNDCARD INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*************************\n"));

		SoundCardInfo.GetInformation(Wmi);
		const std::vector<HWINFO_SOUNDCARD*>* psSoundCardVector = SoundCardInfo.GetSoundCardArray();

		if( psSoundCardVector )
		{
			for( size_t i = 0; i < psSoundCardVector->size(); ++i )
			{
				HWINFO_SOUNDCARD* pSoundCard = (*psSoundCardVector)[i];
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("SOUND[%zu]"), i + 1);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("HardwareId = %s"), pSoundCard->m_tszHardwareId);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("ProductName = %s"), pSoundCard->m_tszProductName);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("CompanyName = %s"), pSoundCard->m_tszCompanyName);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("HasVolCtrl = %s"), pSoundCard->m_bHasVolCtrl ? _T("Yes") : _T("No"));
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("HasSeparateLRVolCtrl = %s\n"), pSoundCard->m_bHasSeparateLRVolCtrl ? _T("Yes") : _T("No"));
			}
		}
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 8. VIDEO INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*********************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* VIDEO INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*********************\n"));

		VideoCardInfo.GetInformation(Wmi);
		const std::vector<HWINFO_VIDEOCARD*>* psVideoCardVector = VideoCardInfo.GetVideoCardArray();

		if( psVideoCardVector )
		{
			for( size_t i = 0; i < psVideoCardVector->size(); ++i )
			{
				HWINFO_VIDEOCARD* pVideoCard = (*psVideoCardVector)[i];
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("VIDEO[%zu]"), i + 1);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("HardwareId = %s"), pVideoCard->m_tszHardwareId);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Manufacturer = %s"), pVideoCard->m_tszManufacturer);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Description = %s"), pVideoCard->m_tszDescription);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("AdapterString = %s"), pVideoCard->m_tszAdapterString);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("ChipType = %s"), pVideoCard->m_tszChipType);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("DacType = %s"), pVideoCard->m_tszDacType);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("DisplayDrivers = %s"), pVideoCard->m_tszDisplayDrivers);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("MemorySize = %d MB\n"), pVideoCard->m_lMemorySize);
			}
		}
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 9. NETWORKCARD INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("***************************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* NETWORKCARD INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("***************************\n"));

		NetworkCardInfo.GetInformation(Wmi);
		const std::vector<HWINFO_NETWORKCARD*>* psNetworkCardVector = NetworkCardInfo.GetNetworkCardArray();

		if( psNetworkCardVector )
		{
			for( size_t i = 0; i < psNetworkCardVector->size(); ++i )
			{
				HWINFO_NETWORKCARD* pNetworkCard = (*psNetworkCardVector)[i];
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("NETWORKCARD[%zu]"), i + 1);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] NetworkCard Description = %s\n"), pNetworkCard->m_tszDescription);
			}
		}
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 10. PCI INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*******************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* PCI INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*******************\n"));

		// CPciInfo(non-WMI, SetupAPI+PciInfo64.asm/32.asm) - Bus/Device/Function/
		// Class Code까지 정밀하게 채워줌. WMI엔 이 정보에 대응하는 속성이 없어서
		// CWmiPciInfo만으론 이 값들을 얻을 수 없음 - 그래서 둘 다 출력함.
		PciInfo.GetInformation();
		const std::vector<HWINFO_PCIDEVICE*>* psPciVector = PciInfo.GetPciDeviceArray();

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("--------- NON-WMI INFORMATION (CPciInfo, SetupAPI) ---------"));
		if( psPciVector )
		{
			for( size_t i = 0; i < psPciVector->size(); ++i )
			{
				HWINFO_PCIDEVICE* pDev = (*psPciVector)[i];
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("PCI[%zu]"), i + 1);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] PCI Bus:Device.Function = %u:%u.%u"), pDev->m_byBus, pDev->m_byDevice, pDev->m_byFunction);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] PCI VendorId:DeviceId = %04X:%04X"), pDev->m_wVendorId, pDev->m_wDeviceId);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] PCI VendorName = %s"), pDev->m_tszVendorName);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] PCI ClassCode = %02X:%02X:%02X"), pDev->m_byBaseClass, pDev->m_bySubClass, pDev->m_byProgIf);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] PCI Type = %d"), (int)pDev->m_eType);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] PCI Description = %s\n"), pDev->m_tszDescription);
			}
		}

		// CWmiPciInfo(WMI, Win32_PnPEntity) - Bus/Device/Function/Class Code는
		// 항상 0/Unknown으로 남음(WMI에 대응 속성 없음). Description/VendorName/
		// VendorId/DeviceId만 참고용.
		WmiPciInfo.GetInformation(Wmi);
		const std::vector<HWINFO_PCIDEVICE*>* psWmiPciVector = WmiPciInfo.GetPciDeviceArray();

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("--------- WMI INFORMATION (CWmiPciInfo) ---------"));
		if( psWmiPciVector )
		{
			for( size_t i = 0; i < psWmiPciVector->size(); ++i )
			{
				HWINFO_PCIDEVICE* pDev = (*psWmiPciVector)[i];
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("PCI[%zu]"), i + 1);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] PCI VendorId:DeviceId = %04X:%04X"), pDev->m_wVendorId, pDev->m_wDeviceId);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] PCI VendorName = %s"), pDev->m_tszVendorName);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] PCI Description = %s\n"), pDev->m_tszDescription);
			}
		}
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 11. CDROM INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*********************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* CDROM INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*********************\n"));

		CdromInfo.GetInformation(Wmi);
		const std::vector<HWINFO_CDROM*>* psCdromVector = CdromInfo.GetCdromArray();

		if( psCdromVector )
		{
			for( size_t i = 0; i < psCdromVector->size(); ++i )
			{
				HWINFO_CDROM* pCdrom = (*psCdromVector)[i];
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("CDROM[%zu]"), i + 1);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Cdrom Name = %s"), pCdrom->m_tszName);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Cdrom Manufacturer = %s"), pCdrom->m_tszManufacturer);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Cdrom Description = %s\n"), pCdrom->m_tszDescription);
			}
		}
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 12. KEYBOARD INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("************************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* KEYBOARD INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("************************\n"));

		KeyBoardInfo.GetInformation(Wmi);

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("KeyBoard Description = %s"), KeyBoardInfo.GetDescription());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("KeyBoard Type = %s\n"), KeyBoardInfo.GetType());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 13. MOUSE INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*********************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* MOUSE INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("*********************\n"));

		MouseInfo.GetInformation(Wmi);

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Mouse Name = %s"), MouseInfo.GetName());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Mouse Manufacturer = %s"), MouseInfo.GetManufacturer());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Mouse Description = %s\n"), MouseInfo.GetDescription());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 14. MONITOR INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("***********************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* MONITOR INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("***********************\n"));

		MonitorInfo.GetInformation(Wmi);
		const std::vector<HWINFO_MONITOR*>* psMonitorVector = MonitorInfo.GetMonitorArray();

		if( psMonitorVector )
		{
			for( size_t i = 0; i < psMonitorVector->size(); ++i )
			{
				HWINFO_MONITOR* pMonitor = (*psMonitorVector)[i];
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("MONITOR[%zu]"), i + 1);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Monitor Manufacturer = %s"), pMonitor->m_tszManufacturer);
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[#] Monitor Description = %s\n"), pMonitor->m_tszDescription);
			}
		}
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 15. OS INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("******************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* OS INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("******************\n"));

		OsInfo.GetInformation();

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Description = %s"), OsInfo.GetDescription());

		if( OsInfo.Is32bitPlatform() )
			LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Bit Platform = 32Bit Platform"));
		else if( OsInfo.Is64bitPlatform() )
			LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Bit Platform = 64Bit Platform"));

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("BuildNumber = %d"), OsInfo.GetBuildNumber());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("MajorVersion = %d"), OsInfo.GetMajorVersion());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("MinorVersion = %d"), OsInfo.GetMinorVersion());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("ServicePack = %s\n"), OsInfo.GetServicePack());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 16. IE INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("******************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* IE INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("******************\n"));

		IeInfo.GetInformation();

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("IE Build = %s"), IeInfo.GetBuild());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("IE Version = %s\n"), IeInfo.GetVersion());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 17. DIRECTX INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("***********************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* DIRECTX INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("***********************\n"));

		DirectXInfo.GetInformation();

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("DirectX Version = %s"), DirectXInfo.GetVersion());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("DirectX Install Version = %s"), DirectXInfo.GetInstallVersion());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("DirectX Description = %s\n"), DirectXInfo.GetDescription());
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

		// 18. JAVAVM INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("**********************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* JAVAVM INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("**********************\n"));

		JavaVMInfo.GetInformation();

		if( JavaVMInfo.IsJVM() == 0 )
			LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Not Run Java Virtual Machine"));
		else if( JavaVMInfo.IsJVM() == 1 )
			LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Run MS Java Virtual Machine"));
		else if( JavaVMInfo.IsJVM() == 2 )
			LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Run SUN Java Virtual Machine"));
		else if( JavaVMInfo.IsJVM() == 3 )
			LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("Run MS, SUN Java Virtual Machine"));

		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("\n---------------------------------------------------------\n"));

		// 19. INSTALL SOFTWARE INFORMATION
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("********************************"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("* INSTALL SOFTWARE INFORMATION *"));
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("********************************\n"));

		InstallSwInfo.GetInformation();
		const std::vector<INSTALL_SWINFO*>* psInstallSwInfoVector = InstallSwInfo.GetInstallSwInfoArray();

		if( psInstallSwInfoVector )
		{
			for( size_t i = 0; i < psInstallSwInfoVector->size(); ++i )
			{
				INSTALL_SWINFO* pInstallSwInfo = (*psInstallSwInfoVector)[i];
				LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("[%zu]. %s"), i + 1, pInstallSwInfo->m_tszDisplayName);
			}
		}
		LOG_WRITE(ELOG_TYPE::LOG_TYPE_INFO, false, _T("\n---------------------------------------------------------\n"));

#ifdef _DEBUG
		PauseConsole(); ClearConsoleScreen();
#endif

	} // Wmi 소멸 (COM이 아직 살아있는 상태에서 안전하게 Release())

	CoUninitialize();

	// Release 빌드는 _CONSOLE_LOG가 꺼지고 _FILE_LOG만 켜져 있어(BaseDefine.h)
	// 각 섹션의 상세 내용이 콘솔에 전혀 출력되지 않습니다. 사용자가 프로그램이
	// 정상적으로 끝났는지, 결과를 어디서 확인해야 하는지 알 수 있도록 결과 파일이
	// 저장된 디렉터리 경로만 짧게 안내합니다. _DEBUG 빌드는 이미 각 섹션 내용이
	// 콘솔에 전부 출력되었으므로 이 안내는 필요 없습니다.
#ifndef _DEBUG
	_tprintf(_T("정보 파일이 생성되었습니다: %s\n"), strLogPath.c_str());
	CloseConsole();
#endif

	return 0;
}