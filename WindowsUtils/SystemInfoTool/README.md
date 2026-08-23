# SystemInfoTool.cpp 상세 설명

## 1. 개요

`SystemInfoTool.cpp`는 Windows 시스템의 하드웨어/소프트웨어 정보를 WMI(Windows Management Instrumentation), CPUID, SetupAPI, 각종 Win32 API를 통해 수집하여, 로그 형태로 콘솔·파일에 출력하는 **콘솔 애플리케이션의 진입점(entry point)**입니다.

CPU, BIOS, 메인보드, 메모리, 디스크, 사운드카드, 그래픽카드, 네트워크카드, **PCI 버스 장치**, CD-ROM, 키보드, 마우스, 모니터, OS, IE, DirectX, JVM, 설치된 소프트웨어 목록까지 총 **19개 항목**을 순차적으로 조회하여 출력합니다.

같은 카테고리라도 정보 출처가 두 갈래로 나뉩니다:
- **WMI 버전**: `System/WmiHardwareInfo.h`의 `CWmi*` 클래스들 (`CWmiBiosInfo`, `CWmiMainBoardInfo`, `CWmiMemoryInfo`, `CWmiHdDiskInfo`, `CWmiDriveInfo`, `CWmiSoundCardInfo`, `CWmiVideoCardInfo`, `CWmiNetworkCardInfo`, `CWmiPciInfo`, `CWmiCdromInfo`, `CWmiKeyBoardInfo`, `CWmiMouseInfo`, `CWmiMonitorInfo`)
- **non-WMI 버전**: CPUID(`System/CpuInfo.h`의 `CCpuInfo`), SetupAPI+어셈블리(`System/PciInfo.h`의 `CPciInfo`)

CPU와 PCI는 두 버전이 서로 대체 불가능한 정보를 주기 때문에(CPU: 명령어 세트 지원 여부 vs 코어/캐시 수치, PCI: Bus/Device/Function 정밀도 vs WMI 자체가 그 정보를 안 줌) **이 두 항목만 WMI/non-WMI를 함께 출력**합니다. 나머지 카테고리는 WMI 하나만 씁니다(Sound/Video는 과거 SetupAPI 버전이 있었으나, WMI가 `ChipType`/`DacType`/볼륨 제어 플래그 등을 실제로 채워줘서 WMI로 통일했습니다).

## 2. 전체 실행 흐름

```
main()
 ├─ 1. 디버그 메모리 누수 감지 설정 (_CRTDBG)
 ├─ 2. 콘솔 UTF-8 인코딩 설정 (Windows 전용)
 ├─ 3. 각 정보 수집 클래스 인스턴스 선언 (CPU가 맨 위, 이어서 Wmi 클래스들)
 ├─ 4. winmgmt(WMI) 서비스를 자동 시작으로 설정
 ├─ 5. 로그 매니저(CLogManager) 초기화
 ├─ 6. COM 라이브러리 초기화 (CoInitializeEx)
 ├─ 7. COM 보안 설정 (CoInitializeSecurity)
 ├─ 8. { 중첩 스코프 시작
 │    ├─ CWmi 생성 및 Connect()
 │    ├─ 1~19번 정보 수집 및 LOG_WRITE 출력 (총 19개 섹션)
 │    │    └─ (디버그 빌드에서는 매 섹션마다 일시정지 + 화면 클리어, JAVAVM 섹션만 예외)
 │    } ← Wmi 소멸 (COM이 살아있는 상태에서 안전하게 Release)
 ├─ 9. CoUninitialize()
 ├─ 10. (릴리즈 빌드만) 결과 파일 경로 안내 + 콘솔 종료
 └─ 11. return 0
```

## 3. 코드 섹션별 상세 설명

### 3.1 헤더 및 매크로

```cpp
#include <pch.h>
#include <iostream>
#include <vector>
#include <bitset>
#include <array>
#include <string>
#include <intrin.h>

#define VS_SEVICE_TITLE _T("winmgmt")
```

- `pch.h`(precompiled header) 하나로 이 파일에서 쓰는 모든 클래스·매크로·타입이 들어옵니다. 실제 include 체인은 다음과 같습니다(자세한 내용은 4절 참고):
  - `BaseDefine.h` → 상수 정의(`NUMERIC_STRING_LEN` 등)
  - `BaseRedefineDataType.h` → `_tstring`, `int8`~`uint64` 등 크로스플랫폼 타입 및 전역 `using namespace std;`(4.3절 참고)
  - `BaseMacro.h` → `SAFE_DELETE` 등 매크로
  - `Util/Log.h` → `LOG_WRITE`/`LOG_ERROR`/`CLogManager`/`ELOG_TYPE`
  - `Util/ConsoleUtil.h` → `InitUtf8Console()`/`ClearConsoleScreen()`/`PauseConsole()`
  - `System/HwInfoStructs.h` → `HWINFO_CPU`, `HWINFO_BIOS`, `HWINFO_RAM` 등 WMI/non-WMI 양쪽이 공유하는 데이터 구조체 전부
  - `System/CpuInfo.h` → `CCpuInfo`(CPUID 기반, non-WMI)
  - `System/WmiHardwareInfo.h` → `CWmi*` 클래스 전부(WMI 기반)
  - `System/PciInfo.h` → `CPciInfo`(SetupAPI+어셈블리, non-WMI)
  - `System/OsInfo.h`, `System/SoftwareInfo.h` → `COsInfo`, `CIeInfo`/`CDirectXInfo`/`CJavaVMInfo`/`CInstallSwInfo`
- `VS_SEVICE_TITLE`은 WMI 핵심 서비스인 `winmgmt`의 서비스 이름입니다.
- 이 파일 자체에는 `using namespace std;`를 별도로 선언하지 않습니다 — `pch.h`가 include하는 `BaseRedefineDataType.h`에 이미 전역 선언이 있어(4.3절), `std::` 접두사 없이 표준 라이브러리 심볼을 쓸 수 있는 상태가 `pch.h` 한 줄만으로 갖춰집니다.

### 3.2 진입점 초기화

```cpp
int main(int argc, char* argv[])
{
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_LEAK_CHECK_DF);
#endif
```
- MSVC 디버그 런타임에서 프로그램 종료 시 메모리 누수를 자동으로 검사·보고하도록 설정합니다. 릴리즈 빌드에는 영향이 없습니다.

```cpp
	InitUtf8Console();
```
- C 런타임 로케일과 콘솔 입출력 코드페이지를 모두 UTF-8(65001)로 맞춰, 한글을 포함한 유니코드 문자열이 콘솔에서 깨지지 않고 올바르게 표시/입력되도록 합니다.
- `InitUtf8Console()`은 `Util/ConsoleUtil.h`에 정의된 `inline` 함수입니다(자세한 내용은 4.5절 참고).

### 3.3 정보 수집 클래스 인스턴스 선언

```cpp
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
```

`CpuInfo`가 제일 위에 선언되어 있고, 실제 출력 순서(3.9절)도 CPU가 1번 섹션입니다 — CPU 관련 코드를 BIOS보다 앞에 두기로 한 정리 방침에 맞춘 배치입니다.

| 클래스 | 정의 헤더 | WMI 사용 | 정보 출처 | 설명 |
|---|---|:---:|---|---|
| `CCpuInfo` | `System/CpuInfo.h` | ✗ | CPUID 명령어 + `GetLogicalProcessorInformationEx` | 제조사·모델명, 클럭 속도, 논리/물리 프로세서 수, Family/Model/Stepping, L2/L3 캐시 크기, ProcessorId, 명령어 세트 지원 여부 |
| `CWmiBiosInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_BIOS` | 제조사, SMBIOS 버전, BIOS 버전, 식별 코드, 시리얼 번호, 출시일 |
| `CWmiMainBoardInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_BaseBoard` | 제품명, 시리얼 번호, 제조사, 설명 |
| `CWmiMemoryInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_PhysicalMemory`, `Win32_OperatingSystem` + `GlobalMemoryStatusEx` | 장착 RAM 모듈별 상세 정보(뱅크·용량·제조사·타입·속도) 및 시스템 전체 메모리/가상메모리/페이지파일 통계 |
| `CWmiHdDiskInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_DiskDrive` | 물리 디스크별 모델명, 제조사, 설명, 시리얼 번호, 버스 타입, 총 용량 |
| `CWmiDriveInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_LogicalDisk` | 논리 드라이브(`C:`, `D:` 등)별 파일시스템, 전체/여유 공간 |
| `CWmiSoundCardInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_SoundDevice` + WinMM API | 볼륨 제어 지원 여부, 제품명, 회사명, HardwareId |
| `CWmiVideoCardInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_VideoController` | 그래픽카드 설명, 제조사, 어댑터 문자열, 칩타입, DAC 타입, 드라이버, 메모리 크기(MB) |
| `CWmiNetworkCardInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_NetworkAdapterConfiguration` | 네트워크 어댑터 설명 |
| `CPciInfo` | `System/PciInfo.h` | ✗ | SetupAPI + `PciInfo64.asm`/`PciInfo86.asm` | PCI 장치별 Bus:Device.Function, Vendor/Device ID, Class Code, GPU/NVMe 분류, 설명 |
| `CWmiPciInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_PnPEntity` | PCI 장치 Description/제조사/Vendor·Device ID (Bus/Device/Function/Class Code는 WMI에 대응 속성이 없어 항상 기본값) |
| `CWmiCdromInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_CDROMDrive` | 광학 드라이브 이름, 제조사, 설명 |
| `CWmiKeyBoardInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_Keyboard` + `GetKeyboardType` API | 키보드 설명, 레이아웃/인터페이스 유형 |
| `CWmiMouseInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_PointingDevice` | 마우스 이름, 제조사, 설명 |
| `CWmiMonitorInfo` | `System/WmiHardwareInfo.h` | ✓ | `Win32_DesktopMonitor` | 모니터 제조사, 설명 |
| `COsInfo` | `System/OsInfo.h` | ✗ | `GetVersionEx`/`RtlGetVersion`, 레지스트리 | OS 버전·에디션 판별, 32/64비트 여부, 빌드 번호, 서비스팩 |
| `CIeInfo` | `System/SoftwareInfo.h` | ✗ | 레지스트리 | Internet Explorer 빌드, 버전 |
| `CDirectXInfo` | `System/SoftwareInfo.h` | ✗ | 레지스트리 | DirectX 버전, 설치 버전, 설명 |
| `CJavaVMInfo` | `System/SoftwareInfo.h` | ✗ | 레지스트리 + 파일 시스템 탐색 | MS/SUN JVM 설치 여부(상태 코드 0~3) |
| `CInstallSwInfo` | `System/SoftwareInfo.h` | ✗ | 레지스트리(`Uninstall` 키) | 설치된 소프트웨어 표시 이름 목록 |

`HWINFO_CPU`, `HWINFO_PCIDEVICE`를 포함한 모든 `HWINFO_*` 구조체는 `System/HwInfoStructs.h` 한 곳에 있고, WMI 클래스와 non-WMI 클래스가 **같은 타입**을 채워 넣습니다(예: `CCpuInfo`와 `CWmiProcessorInfo`가 둘 다 `HWINFO_CPU*`를 다룸 — `CWmiProcessorInfo`는 현재 이 도구에서는 안 쓰지만 라이브러리에는 남아있음). 그래서 CPU/PCI를 제외한 카테고리는 WMI 버전 하나만 골라 써도 호출부 코드(`Get*Array()` 반환 타입 등)가 그대로 유지됩니다.

### 3.4 winmgmt 서비스 자동 시작 설정

```cpp
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
```
- 이후 WMI 쿼리가 정상 동작하려면 `winmgmt` 서비스가 실행 중이어야 하므로, 이 서비스의 시작 유형을 **자동 시작(`SERVICE_AUTO_START`)**으로 미리 바꿔둡니다.
- 이 블록이 하는 일은 기존 서비스의 시작 유형을 바꾸는 것뿐이라 `SC_MANAGER_CONNECT` 권한이면 충분합니다(서비스를 새로 만드는 게 아니므로 `SC_MANAGER_CREATE_SERVICE`는 불필요).
- `hScm`/`hService`는 실패 시 `NULL`을 반환하므로, 둘 다 사용 전에 유효성을 체크합니다 — 관리자 권한이 없는 환경에서는 `OpenSCManager`나 `OpenService`가 실패할 수 있는데, 이 경우 블록 전체가 조용히 건너뛰어지고 프로그램은 계속 진행됩니다(치명적 오류로 취급하지 않음).
- `ChangeServiceConfig`의 반환값(`BOOL`)은 검사하지 않으므로, 이 호출 자체가 실패해도 알 수 없습니다(6절 참고).

### 3.5 로그 매니저 초기화

```cpp
// EXE가 위치한 디렉터리를 구해 그 아래에 "Log\" 경로를 만듭니다.
TCHAR tszExePath[MAX_PATH] = { 0, };
DWORD dwLen = GetModuleFileName(NULL, tszExePath, MAX_PATH);
if( dwLen == 0 || dwLen == MAX_PATH )
{
	_tcscpy_s(tszExePath, MAX_PATH, _T(".\\"));
}

_tstring strExePath = tszExePath;
size_t nPos = strExePath.find_last_of(_T("\\/"));
_tstring strExeDir = (nPos != _tstring::npos) ? strExePath.substr(0, nPos + 1) : _T(".\\");

_tstring strLogPath = strExeDir + _T("Log\\");

CreateDirectory(strLogPath.c_str(), NULL);

CLogManager::Instance().Create(strLogPath.c_str());
```
- 실행 파일 자신의 전체 경로를 `GetModuleFileName(NULL, ...)`으로 얻은 뒤, 마지막 `\`/`/` 위치까지 잘라 **exe가 있는 디렉터리 밑의 `Log\`** 경로를 로그 저장 위치로 사용합니다.
- `dwLen == 0`(조회 실패)이거나 `dwLen == MAX_PATH`(경로가 잘렸을 가능성)인 경우, 현재 작업 디렉터리(`.\`)로 안전하게 대체합니다.
- `CLog::Write()`는 대상 디렉터리를 자동으로 만들어주지 않고, `fopen`이 실패하면 에러 표시 없이 그냥 리턴합니다 — 폴더가 없으면 로그가 조용히 전부 유실됩니다. 그래서 `CLogManager::Instance().Create()` 호출 전에 `CreateDirectory()`를 먼저 호출해 폴더를 만들어 둡니다.
- `CLog::_tszDirectory`는 `TCHAR[DIRECTORY_STRLEN]`(256) 고정 버퍼이고 내부에서 `_tcsncpy_s(..., _TRUNCATE)`로 안전하게 잘라 복사하므로, `strLogPath`가 256자를 넘으면 잘린 채로 저장될 수 있다는 점만 유의하면 됩니다(오버플로우는 없음).

### 3.6 COM 초기화 및 보안 설정

```cpp
HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
if( FAILED(hrCom) )
{
	LOG_ERROR(_T("CoInitializeEx Failed. HRESULT: 0x%08X"), hrCom);
	return -1;
}

HRESULT hr = CoInitializeSecurity(
	NULL, -1, NULL, NULL,
	RPC_C_AUTHN_LEVEL_DEFAULT,
	RPC_C_IMP_LEVEL_IMPERSONATE,
	NULL, EOAC_NONE, NULL
);

if( FAILED(hr) )
{
	LOG_ERROR(_T("CoInitializeSecurity Failed. HRESULT: 0x%08X"), hr);
	CoUninitialize();
	return -1;
}
```
- **`CoInitializeEx`**: 현재 스레드에서 COM 라이브러리를 멀티스레드 아파트먼트(MTA) 모델로 초기화합니다. WMI는 COM 기반이므로 `CWmi`를 쓰기 전에 반드시 COM이 초기화되어 있어야 합니다.
- **`CoInitializeSecurity`**: 프로세스 차원의 COM 보안 수준(인증 레벨, 위장 레벨 등)을 한 번만 설정합니다.
- 두 호출 모두 실패 시 `LOG_ERROR`로 원인(HRESULT)을 남기고 `-1`을 반환하며 프로그램을 종료합니다.
- **COM 생명주기 설계**: `CoInitializeEx`/`CoUninitialize`를 프로세스(main 함수) 차원에서 한 번만 관리하고, `CWmi` 클래스 자신은 COM을 초기화하지 않는 구조입니다.

### 3.7 CWmi 스코프와 정보 수집 본문

```cpp
{
	CWmi Wmi;

	if( !Wmi.Connect() )
	{
		LOG_ERROR(_T("WMI Connection Failed."));
		CoUninitialize();
		return -1;
	}

	// 1~19번 섹션...

} // Wmi 소멸 (COM이 아직 살아있는 상태에서 안전하게 Release())

CoUninitialize();
```

**핵심 설계 포인트 — 중첩 스코프(`{ }`)의 역할:**

`CWmi Wmi;`가 별도의 중괄호 블록 안에 있는 것은 의도된 설계입니다.

- C++에서 지역 변수는 함수가 끝날 때가 아니라, **자신이 선언된 스코프(`{ }`)를 벗어나는 시점**에 소멸자가 호출됩니다.
- `CWmi`의 소멸자는 내부적으로 COM 인터페이스(`IWbemLocator`, `IWbemServices` 등)의 `Release()`를 호출하는데, 이 호출이 유효하려면 **그 시점에 COM이 아직 살아있어야** 합니다.
- `Wmi`를 명시적으로 좁은 스코프에 가둬 `}`에서 먼저 소멸시키고, 그 다음 줄에서 `CoUninitialize()`를 호출함으로써 "COM이 살아있는 동안에만 COM 리소스가 정리되도록" 순서를 보장합니다.
- `CCpuInfo`/`CPciInfo`는 WMI를 안 쓰는데도 이 스코프 안에서 호출됩니다 — 출력 섹션 순서를 하나로 관리하기 위한 배치일 뿐, `Wmi`가 없어도 두 클래스는 정상 동작합니다.

### 3.8 19개 정보 수집 섹션 공통 패턴

각 섹션은 대체로 다음과 같은 동일한 패턴을 따릅니다:

```cpp
// N. XXX INFORMATION
LOG_WRITE(..., _T("*** 구분선/제목 ***"));

XxxInfo.GetInformation(Wmi);              // (WMI 미사용 클래스는 인자 없음)
const std::vector<HWINFO_XXX*>* pVector = XxxInfo.GetXxxArray();  // 배열형 정보만 해당

if( pVector )
{
	for( size_t i = 0; i < pVector->size(); ++i )
	{
		// 항목별 LOG_WRITE 출력
	}
}

LOG_WRITE(..., _T("--- 구분선 ---\n"));

#ifdef _DEBUG
	PauseConsole(); ClearConsoleScreen();
#endif
```

**항목별 형태 분류:**

| 유형 | 특징 | 해당 항목 |
|---|---|---|
| 단일 값 | `GetXxxInfo()` 호출 후 Getter들로 값 하나씩 출력 | PROCESSOR, BIOS, MAINBOARD, KEYBOARD, MOUSE, OS, IE, DIRECTX |
| 배열(포인터 벡터) | `Get*Array()`로 `vector<T*>`를 받아 반복 출력 | MEMORY(RAM), DRIVES, LOGICAL DISK, SOUNDCARD, VIDEO, NETWORKCARD, PCI, CDROM, MONITOR, INSTALL SOFTWARE |
| 특수 처리 | 별도 가공 로직 또는 이중 출력 포함 | PROCESSOR(문자열 치환), PCI(CPciInfo+CWmiPciInfo 두 번 출력), JAVAVM(분기 출력) |

`_DEBUG` 빌드에서만 각 섹션 뒤에 `PauseConsole()`/`ClearConsoleScreen()`을 실행합니다(릴리즈 빌드에서는 이 블록 자체가 컴파일에서 제외됨). 19개 섹션 중 **JAVAVM 섹션만 예외적으로 이 블록이 없어**, 곧바로 INSTALL SOFTWARE 섹션으로 이어집니다 — 총 **18곳**에서 반복됩니다.

### 3.9 섹션별 세부 내용

#### ① PROCESSOR INFORMATION
```cpp
CpuInfo.GetInformation();
```
CPU 브랜드 문자열에서 연속 공백 2칸을 제거하는 정리 로직을 거친 뒤, `Vendor Name`, `Processor Name`, `Processor Id`(CPUID Leaf 1의 EDX:EAX 16진수 결합), `Speed(MHz)`, `Processors Count (Logical)`, `Processors Count (Physical Cores)`(`GetLogicalProcessorInformationEx` 기반), `Signature Info`(Family/Model/Stepping), `L2CacheSize`/`L3CacheSize`(`cpu_cache_size_kb()` 기반), MMX/SSE/SSE2/3DNow! 지원 여부를 출력합니다. `CCpuInfo`는 WMI를 쓰지 않고 CPUID와 `GetLogicalProcessorInformationEx`만으로 정보를 얻습니다.

#### ② BIOS INFORMATION
```cpp
BiosInfo.GetInformation(Wmi);
```
`Win32_BIOS`에서 `Manufacturer`, `SmVersion`, `Version`, `IdentificationCode`, `SerialNumber`, `ReleaseDate` 6개 항목을 조회합니다. `IdentificationCode`는 WMI 자체가 대부분의 시스템에서 빈 값으로 두는 필드라(SMBIOS에 대응 항목이 없는 WMI 파생 속성), 이 도구의 문제가 아니라 정상적으로 비어 있을 수 있습니다.

#### ③ MAINBOARD INFORMATION
```cpp
MainBoardInfo.GetInformation(Wmi);
```
`Win32_BaseBoard`에서 `Product`, `SerialNumber`, `Manufacturer`, `Description` 4개 항목을 조회합니다.

#### ④ MEMORY INFORMATION — 가장 복잡한 섹션
```cpp
MemoryInfo.GetInformation(Wmi);
const std::vector<HWINFO_RAM*>* psRamVector = MemoryInfo.GetRamArray();
```
- 장착된 RAM 모듈 각각(`HWINFO_RAM`)에 대해 `BankLabel`, `Name`, `Manufacturer`, `DeviceLocator`, 용량(`ChangeDataFormat`으로 사람이 읽기 좋은 단위로 변환), `FormFactor`, `MemoryType`, `Speed`를 개별 출력합니다.
- 이어서 시스템 전체 메모리 요약(`Total RAM Count`, `Total/Physical/Used Memory Size`, `Total/Free Virtual Memory Size`, `Total/Free PageFile Size`)을 출력합니다.

#### ⑤ DRIVES INFORMATION (물리 디스크)
```cpp
HdDiskInfo.GetInformation(Wmi);
```
각 물리 디스크(`HWINFO_HDDISK`)의 `Model`, `Name`, `Manufacturer`, `Description`, `SerialNumber`, `BusType`, 총 용량을 출력합니다.

#### ⑥ LOGICAL DISK INFORMATION (드라이브 문자)
```cpp
DriveInfo.GetInformation(Wmi);
```
각 논리 드라이브(예: `C:`, `D:`)의 이름, 파일시스템, 전체/여유/사용 공간을 출력한 뒤, 전체 드라이브 총합 통계를 출력합니다.

#### ⑦ SOUNDCARD INFORMATION
```cpp
SoundCardInfo.GetInformation(Wmi);
```
`Win32_SoundDevice` 기반 `CWmiSoundCardInfo`로 `HardwareId`, `ProductName`, `CompanyName`, `HasVolCtrl`, `HasSeparateLRVolCtrl`(좌우 볼륨 독립 제어 여부)을 출력합니다.

#### ⑧ VIDEO INFORMATION (그래픽카드)
```cpp
VideoCardInfo.GetInformation(Wmi);
```
`Win32_VideoController` 기반 `CWmiVideoCardInfo`로 각 그래픽카드의 `HardwareId`, `Manufacturer`, `Description`, `AdapterString`, `ChipType`, `DacType`, `DisplayDrivers`, 메모리 크기(MB)를 출력합니다.

#### ⑨ NETWORKCARD INFORMATION
```cpp
NetworkCardInfo.GetInformation(Wmi);
```
각 네트워크 카드의 `Description`만 출력합니다.

#### ⑩ PCI INFORMATION — CPciInfo + CWmiPciInfo 이중 출력
```cpp
PciInfo.GetInformation();
const std::vector<HWINFO_PCIDEVICE*>* psPciVector = PciInfo.GetPciDeviceArray();
// ... CPciInfo(non-WMI) 출력 ...

WmiPciInfo.GetInformation(Wmi);
const std::vector<HWINFO_PCIDEVICE*>* psWmiPciVector = WmiPciInfo.GetPciDeviceArray();
// ... CWmiPciInfo(WMI) 출력 ...
```
- **`CPciInfo`(non-WMI, SetupAPI+어셈블리)**: `Bus:Device.Function`, `VendorId:DeviceId`, `VendorName`(내장 벤더 테이블), `ClassCode`, `Type`(GPU/NVMe 분류), `Description`을 출력합니다.
- **`CWmiPciInfo`(WMI, `Win32_PnPEntity`)**: `VendorId:DeviceId`, `VendorName`(WMI `Manufacturer` 속성), `Description`만 출력합니다 — Bus/Device/Function/Class Code는 WMI에 대응 속성이 없어 이 클래스는 항상 기본값(0/Unknown)으로 남기 때문에 아예 출력 항목에서 뺐습니다.
- 두 클래스는 `HWINFO_PCIDEVICE` 타입을 공유하므로, 같은 반복문 구조를 그대로 재사용할 수 있습니다.

#### ⑪ CDROM INFORMATION
```cpp
CdromInfo.GetInformation(Wmi);
```
각 광학 드라이브의 `Name`, `Manufacturer`, `Description`을 출력합니다.

#### ⑫ KEYBOARD INFORMATION / ⑬ MOUSE INFORMATION
단일 키보드/마우스 장치에 대해 각각 `Description`/`Type`(키보드), `Name`/`Manufacturer`/`Description`(마우스)을 출력합니다.

#### ⑭ MONITOR INFORMATION
연결된 각 모니터의 `Manufacturer`, `Description`을 출력합니다.

#### ⑮ OS INFORMATION
```cpp
OsInfo.GetInformation();
```
OS 설명, 32/64비트 여부, 빌드 번호, 메이저/마이너 버전, 서비스팩 정보를 출력합니다.

#### ⑯ IE INFORMATION
```cpp
IeInfo.GetInformation();
```
Internet Explorer의 `Build`, `Version`을 출력합니다.

#### ⑰ DIRECTX INFORMATION
```cpp
DirectXInfo.GetInformation();
```
DirectX `Version`, `InstallVersion`, `Description`을 출력합니다.

#### ⑱ JAVAVM INFORMATION — 분기 출력
```cpp
JavaVMInfo.GetInformation();

if( JavaVMInfo.IsJVM() == 0 )
	LOG_WRITE(..., _T("Not Run Java Virtual Machine"));
else if( JavaVMInfo.IsJVM() == 1 )
	LOG_WRITE(..., _T("Run MS Java Virtual Machine"));
else if( JavaVMInfo.IsJVM() == 2 )
	LOG_WRITE(..., _T("Run SUN Java Virtual Machine"));
else if( JavaVMInfo.IsJVM() == 3 )
	LOG_WRITE(..., _T("Run MS, SUN Java Virtual Machine"));
```
`IsJVM()`이 반환하는 정수 코드(0~3)에 따라 4가지 메시지 중 하나를 출력합니다. 19개 섹션 중 유일하게 이 섹션 뒤에는 `#ifdef _DEBUG` 일시정지 블록이 없어, 곧바로 다음 섹션(⑲)으로 이어집니다.

#### ⑲ INSTALL SOFTWARE INFORMATION
```cpp
InstallSwInfo.GetInformation();
const std::vector<INSTALL_SWINFO*>* psInstallSwInfoVector = InstallSwInfo.GetInstallSwInfoArray();

if( psInstallSwInfoVector )
{
	for( size_t i = 0; i < psInstallSwInfoVector->size(); ++i )
	{
		INSTALL_SWINFO* pInstallSwInfo = (*psInstallSwInfoVector)[i];
		LOG_WRITE(..., _T("[%zu]. %s"), i + 1, pInstallSwInfo->m_tszDisplayName);
	}
}
```
시스템에 설치된 소프트웨어 목록(레지스트리 `Uninstall` 키 기반)을 번호를 매겨 나열합니다. 실제 마지막 섹션임에도 `#ifdef _DEBUG` 일시정지 블록이 붙어 있어, `Wmi` 스코프가 닫히기 직전에 `PauseConsole()`/`ClearConsoleScreen()`이 한 번 더 실행됩니다.

### 3.10 종료 처리

```cpp
	} // Wmi 소멸 (COM이 아직 살아있는 상태에서 안전하게 Release())

	CoUninitialize();

#ifndef _DEBUG
	_tprintf(_T("정보 파일이 생성되었습니다: %s\n"), strLogPath.c_str());
	CloseConsole();
#endif

	return 0;
```
- `Wmi`가 스코프를 벗어나며 소멸 → COM 인터페이스 정상 해제
- `CoUninitialize()`로 COM 라이브러리 정리
- **릴리즈 빌드에서만** 결과가 저장된 로그 디렉터리 경로(3.5절에서 계산해 둔 `strLogPath`)를 안내합니다. 릴리즈 빌드는 `_CONSOLE_LOG`가 꺼지고 `_FILE_LOG`만 켜져 있어(4.1절 참고) 각 섹션의 상세 내용이 콘솔에 전혀 출력되지 않으므로, 프로그램이 정상적으로 끝났는지·결과를 어디서 확인해야 하는지 알려주는 마지막 안내가 필요합니다.
- 정상 종료 시 `0` 반환

## 4. 프로젝트 공통 인프라 (`pch.h` 및 공용 헤더)

### 4.1 `pch.h` — 전역 사전 컴파일 헤더

```cpp
#define WIN32_LEAN_AND_MEAN
#define _HAS_STD_BYTE 0

#include <windows.h> ... <mmsystem.h>
#include <BaseDefine.h>
#include <BaseRedefineDataType.h>
#include <BaseMacro.h>

#include <Util/WinCharsetConv.h>
#include <Util/EncodingConvert.h>
#include <Util/StringUtil.h>

#ifdef _DEBUG
#define _CONSOLE_LOG
#else
#define _FILE_LOG
#endif

#include <Util/Log.h>
#include <Util/ConsoleUtil.h>

#include <System/SystemBaseDefine.h>
#include <System/HwInfoStructs.h>
#include <System/Wmi.h>
#include <System/OsInfo.h>
#include <System/SoftwareInfo.h>
#include <System/CpuInfo.h>
#include <System/WmiHardwareInfo.h>
#include <System/PciInfo.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "version.lib")

#define  _DEBUGLOG
```

- **`WIN32_LEAN_AND_MEAN`**: `<windows.h>`가 포함하는 부가 기능을 빼서 컴파일 속도를 높이는 표준 관행입니다.
- **`_HAS_STD_BYTE 0`**: C++17의 `std::byte`가 이 프로젝트에서 쓰는 `BYTE`/커스텀 바이트 타입과 이름 충돌을 일으키는 것을 막기 위해 비활성화합니다.
- **`_CONSOLE_LOG`/`_FILE_LOG` 빌드별 전환**: `Util/Log.h`(`CLog::Write()`)가 참조하는 로그 출력 대상 매크로를 `_DEBUG` 정의 여부로 자동 전환합니다 — 디버그는 콘솔에만, 릴리즈는 파일에만 출력합니다.
- **`System/HwInfoStructs.h`가 `System/Wmi.h`보다 먼저 include**됩니다 — `HWINFO_*` 구조체들이 WMI 클래스와 non-WMI 클래스 양쪽에서 쓰이는 공용 타입이라, 어느 한쪽 헤더에 종속되지 않도록 가장 먼저 옵니다.
- **`System/CpuInfo.h` → `System/WmiHardwareInfo.h` → `System/PciInfo.h`** 순서: `CpuInfo.h`가 `HwInfoStructs.h`의 `HWINFO_CPU`를 그대로 쓰고, `WmiHardwareInfo.h`도 같은 `HWINFO_CPU`를 쓰는 `CWmiProcessorInfo`를 선언합니다. `PciInfo.h`는 `HWINFO_PCIDEVICE`를 씁니다 — 셋 다 구조체 정의 자체는 갖고 있지 않고 `HwInfoStructs.h`만 참조합니다.
- **`#pragma comment(lib, "winmm.lib")`**: `timeGetTime()`을 링크하기 위함입니다 — `CCpuInfo::CalculateCpuSpeedMethod2()`(CPU 클럭 속도 측정 폴백 경로)에서 사용됩니다.
- **`#pragma comment(lib, "version.lib")`**: `GetFileVersionInfoSize()`/`GetFileVersionInfo()`/`VerQueryValue()`를 링크하기 위함입니다 — `SoftwareInfo.cpp`의 `GetVersionLangOfFile()`(JVM 버전 조회 등에 사용)에서 사용됩니다.
- **`_DEBUGLOG`**: "모니터링을 위한 로깅 활성화" 스위치로, `Util/Log.h` 내부에서 이 매크로 정의 여부로 로그 출력 동작을 켜고 끄는 것으로 추정됩니다.

### 4.2 `BaseDefine.h` — 전역 상수

| 상수 | 값 | `SystemInfoTool.cpp`에서의 용도 |
|---|---|---|
| `NUMERIC_STRING_LEN` | `20` | `TCHAR tszFormat[NUMERIC_STRING_LEN];` — `ChangeDataFormat()`이 채우는 문자열 버퍼 크기 |

### 4.3 `BaseRedefineDataType.h` — 크로스플랫폼 타입 정의

- **`_tstring`**: `UNICODE` 정의 시 `std::wstring`, 아니면 `std::string`.
- **`TCHAR`**: Windows 빌드에서는 `<tchar.h>`가 제공하는 타입을 그대로 사용.
- 이 헤더 안에 `using namespace std;`가 있어, `SystemInfoTool.cpp`가 별도로 선언하지 않아도 `pch.h`를 통해 이미 전역에 영향을 미칩니다.

### 4.4 `BaseMacro.h` — 매크로 모음

| 매크로 | 역할 |
|---|---|
| `SAFE_DELETE(p)` / `SAFE_DELETE_ARRAY(p)` | NULL 체크 후 `delete`/`delete[]`, 포인터를 `nullptr`로 초기화 |
| `CRASH` / `CRASH_CAUSE(cause)` / `ASSERT_CRASH(expr)` | 의도적으로 크래시 덤프를 발생시키는 디버깅용 매크로 |
| `_tmemset`/`_tmemcpy`/`__TFUNCTION__` | `UNICODE` 여부에 따라 `memset`/`wmemset` 등으로 분기 |
| `_GetTickCount` | Windows 버전에 따라 `GetTickCount64`/`GetTickCount`로 분기 |
| `LIB_NAME(LIB)` | 빌드 구성(x86/x64, Debug/Release)에 맞는 라이브러리 파일명 문자열 생성 |

### 4.5 `Util/ConsoleUtil.h` — 콘솔 유틸리티

`InitUtf8Console()`, `ClearConsoleScreen()`, `PauseConsole()` 세 함수를 한데 모은 헤더입니다. 모두 `inline` 함수인 이유:

1. **재사용성**: 콘솔을 다루는 다른 도구에서도 필요할 가능성이 높은 범용 유틸리티라 한 곳에 모아둡니다.
2. **매크로 대신 함수로 통일**: 호출부가 많은(이 파일 기준 18회) 로직을 매크로로 두면 전처리기가 호출부마다 본문을 복사해 바이너리가 불어나고, 매크로/함수 이름 충돌 위험도 있습니다.
3. **헤더에 정의하면서도 여러 `.cpp`에서 문제없이 쓰기 위해**: `inline`을 붙여 여러 번역 단위에 각자 정의가 있어도 링커 단계에서 중복 정의(ODR 위반) 오류가 나지 않도록 합니다.

## 5. 사용되는 매크로 · 유틸리티

| 심볼 | 정의 위치 | 역할 |
|---|---|---|
| `LOG_WRITE(type, flag, fmt, ...)` | `Util/Log.h` | `CLogManager::Instance().Write(...)`를 호출하는 매크로 |
| `LOG_ERROR(fmt, ...)` | `Util/Log.h` | `ELOG_TYPE::LOG_TYPE_ERROR`로 고정된 로그 매크로 |
| `ELOG_TYPE::LOG_TYPE_INFO` | `Util/Log.h` | 로그 레벨 열거형 |
| `CLogManager::Instance()` | `Util/Log.h` | 싱글톤 로그 매니저 |
| `CWmi` | `System/Wmi.h` | WMI 연결/쿼리 래퍼 클래스 |
| `ChangeDataFormat(int64, TCHAR*)` | `System/WmiHardwareInfo.h` | 바이트 수치를 KB/MB/GB 등으로 변환 |
| `_tstring` | `BaseRedefineDataType.h` | `UNICODE` 정의 시 `std::wstring`, 아니면 `std::string` |
| `NUMERIC_STRING_LEN` | `BaseDefine.h` (값 `20`) | `tszFormat` 버퍼 크기 상수 |
| `HWINFO_CPU`, `HWINFO_BIOS`, `HWINFO_PCIDEVICE` 등 | `System/HwInfoStructs.h` | WMI/non-WMI 공용 하드웨어 정보 구조체 |
| `InitUtf8Console()` / `ClearConsoleScreen()` / `PauseConsole()` | `Util/ConsoleUtil.h` | UTF-8 콘솔 설정 / 화면 지우기 / 키 입력 대기 |

## 6. 잠재적 개선 여지 (참고)

- `ChangeServiceConfig`, `CLogManager::Instance().Create()`의 반환값을 검사하지 않아, 실패해도 사용자에게 알리지 않고 넘어갑니다.

## 7. 어셈블리 파일(.asm) 빌드 방법

이 프로젝트는 CPU/PCI 정보 수집 일부를 순수 어셈블리로 구현합니다. 아래 4개 파일이 대상이며, 전부 내용이 준비되어 있습니다.

| 파일 | 구현 함수 | 상태 |
|---|---|---|
| `CpuInfo86.asm` | `cpu_id_supported`, `cpu_id`, `cpu_vendor`, `cpu_brand_part0~5`, `cpu_brand`, `cpu_cache_size_kb`, `cpu_core_type`, `cpu_read_tsc` | ✅ 작성 완료 |
| `CpuInfo64.asm` | 위와 동일 (x64) | ✅ 작성 완료 |
| `PciInfo86.asm` | `pci_parse_vendor_name`, `pci_classify_device` | ✅ 작성 완료 |
| `PciInfo64.asm` | 위와 동일 (x64) | ✅ 작성 완료 |

`GetSmbiosString86.asm`/`GetSmbiosString64.asm`(SMBIOS 문자열 조회), `MemDiskDetail32.asm`/`MemDiskDetail64.asm`(디스크 IOCTL)도 같은 패턴으로 이미 프로젝트에 포함되어 있습니다 — 아래 절차는 이 파일들에도 동일하게 적용됩니다.

### 7.1 Visual Studio 프로젝트에 추가

1. 솔루션 탐색기에서 프로젝트 우클릭 → **추가 → 기존 항목**으로 `.asm` 파일을 추가합니다.
2. 추가한 `.asm` 파일을 우클릭 → **속성** → **일반** → **항목 형식**을 **Microsoft Macro Assembler**로 지정합니다.
   - 기본값이 "인식할 수 없는 파일" 등으로 잡혀 있으면 MASM 도구가 이 파일을 그냥 무시하고, 빌드는 되지만 링크 단계에서 "확인되지 않은 외부 기호(unresolved external symbol)" 오류가 납니다.
3. **86(x86) 파일과 64(x64) 파일은 반드시 분리**합니다 — 같은 함수 이름이라도 호출 규약(x86은 cdecl/stdcall 혼용, x64는 통일된 호출 규약)이 달라 코드가 다르므로, 플랫폼별 소스 파일 자체가 다릅니다.
   - `CpuInfo86.asm`/`PciInfo86.asm` → **구성 속성 → 일반 → 제외된 빌드**에서 **x64 구성만 제외**(또는 솔루션 플랫폼이 Win32일 때만 포함)
   - `CpuInfo64.asm`/`PciInfo64.asm` → 반대로 **x86(Win32) 구성만 제외**
   - Visual Studio 프로젝트 속성 화면에서 상단의 "구성"/"플랫폼" 드롭다운을 바꿔가며 각 파일별로 제외 여부를 지정하면 됩니다.

**위 3단계를 거치면 `.vcxproj` 파일에는 실제로 다음 세 조각이 생깁니다** — `SystemInfoTool.vcxproj`에 이미 이렇게 적용되어 있습니다.

**① 2번 단계(항목 형식 지정)를 처음 한 번 할 때, MASM 빌드 규칙을 불러오는 `<Import>` 두 개가 자동으로 추가됩니다.** 하나는 파일 상단 쪽(다른 `<Import>` 구문들, 보통 `Microsoft.Cpp.props` 바로 아래)에:
```xml
<ImportGroup Label="ExtensionSettings">
  <Import Project="$(VCTargetsPath)\BuildCustomizations\masm.props" />
</ImportGroup>
```
다른 하나는 파일 맨 아래쪽(`Microsoft.Cpp.targets` 이후)에:
```xml
<ImportGroup Label="ExtensionTargets">
  <Import Project="$(VCTargetsPath)\BuildCustomizations\masm.targets" />
</ImportGroup>
```
둘 다 `.vcxproj`당 한 번만 있으면 되므로, 이미 다른 asm 파일 때문에 들어가 있다면 새 asm 파일을 추가해도 이 두 블록이 또 생기진 않습니다.

**② 1~3번 단계(파일 추가 + 항목 형식 + 플랫폼별 제외)의 결과가 `<MASM Include="...">` 항목으로 쌓입니다** — 3번 단계에서 지정한 "제외된 빌드" 설정이 `ExcludedFromBuild` 조건으로 그대로 기록됩니다:
```xml
<ItemGroup>
  <MASM Include="..\..\..\Library\UtilCore\System\Asm\CpuInfo86.asm">
    <ExcludedFromBuild Condition="'$(Platform)'=='x64'">true</ExcludedFromBuild>
  </MASM>
  <MASM Include="..\..\..\Library\UtilCore\System\Asm\CpuInfo64.asm">
    <ExcludedFromBuild Condition="'$(Platform)'=='Win32'">true</ExcludedFromBuild>
  </MASM>
  <MASM Include="..\..\..\Library\UtilCore\System\Asm\PciInfo86.asm">
    <ExcludedFromBuild Condition="'$(Platform)'=='x64'">true</ExcludedFromBuild>
  </MASM>
  <MASM Include="..\..\..\Library\UtilCore\System\Asm\PciInfo64.asm">
    <ExcludedFromBuild Condition="'$(Platform)'=='Win32'">true</ExcludedFromBuild>
  </MASM>
</ItemGroup>
```
- `Condition="'$(Platform)'=='x64'"`가 붙은 86 파일은 x64 빌드에서 제외, `Condition="'$(Platform)'=='Win32'"`가 붙은 64 파일은 Win32 빌드에서 제외 — 3번 단계에서 UI로 클릭한 내용이 정확히 이 조건식입니다.
- 실제 경로(`..\..\..\Library\UtilCore\System\Asm\`)를 보면, 이 프로젝트의 asm 파일들은 `SystemInfoTool.vcxproj`가 있는 위치가 아니라 **공용 라이브러리(`Library\UtilCore\System\Asm\`)에 모여있고, `SystemInfoTool` 프로젝트는 상대 경로로 그걸 참조**하는 구조입니다 — `pch.h`가 include하는 `System/CpuInfo.h`, `System/PciInfo.h` 등과 같은 `System` 폴더 아래 `Asm` 하위 폴더에 실제 `.asm`이 위치한다고 보면 됩니다.
- **`.vcxproj`를 직접 열어 위 XML을 손으로 편집해도 결과는 동일**합니다 — UI 3단계와 XML 편집은 같은 걸 두 가지 방식으로 하는 것뿐입니다. 이미 asm 파일이 몇 개 들어가 있는 프로젝트에 새 파일을 추가할 때는, ①의 `<Import>` 두 개는 이미 있으니 손댈 필요 없이 ②의 `<MASM Include="...">` 하나만 새로 추가하면 더 빠릅니다.

### 7.2 MASM 빌드 도구 확인

- **Visual Studio Installer**에서 "C++를 사용한 데스크톱 개발" 워크로드를 설치했다면 MASM(`ml.exe`/`ml64.exe`)은 기본 포함됩니다. 별도 설치가 필요 없습니다.
- 빌드 로그에서 `ml.exe`(x86) 또는 `ml64.exe`(x64)가 호출되는지 확인하면, 항목 형식이 제대로 잡혔는지(= `<Import>` 두 개가 정상적으로 들어가 있는지) 검증할 수 있습니다.


### 7.2 MASM 빌드 도구 확인

- **Visual Studio Installer**에서 "C++를 사용한 데스크톱 개발" 워크로드를 설치했다면 MASM(`ml.exe`/`ml64.exe`)은 기본 포함됩니다. 별도 설치가 필요 없습니다.
- 빌드 로그에서 `ml.exe`(x86) 또는 `ml64.exe`(x64)가 호출되는지 확인하면, 항목 형식이 제대로 잡혔는지 검증할 수 있습니다.

### 7.3 필요한 링커 설정

| 파일 | 링크해야 하는 라이브러리 | 이유 |
|---|---|---|
| `PciInfo86.asm`/`PciInfo64.asm` | 없음(추가 라이브러리 불필요) | 벤더 테이블 조회/Class Code 비교만 하는 순수 계산 함수라 외부 API 호출이 없음 |
| `CpuInfo86.asm`/`CpuInfo64.asm` | 없음(추가 라이브러리 불필요 — CPUID/RDTSC는 CPU 명령어라 API 호출이 없음) | `.cpp` 쪽(`CalculateCpuSpeedMethod2`)에서 쓰는 `timeGetTime()`은 별도로 `winmm.lib` 필요(이미 `pch.h`에 `#pragma comment`로 링크됨, 4.1절 참고) |

`GetSmbiosString*.asm`/`MemDiskDetail*.asm`은 `GetSystemFirmwareTable`/`HeapAlloc` 등 Kernel32 API를 호출하므로 `Kernel32.lib` 링크가 필요합니다(대부분의 Windows 데스크톱 프로젝트에 기본 포함되어 있어 보통 별도 설정이 필요 없습니다).

### 7.4 빌드 후 확인 순서

1. x86/x64 각각 빌드해서 어셈블 에러부터 잡습니다 — 레이블 중복, `EXTERN` 누락 같은 문법 오류는 이 단계에서 걸러집니다.
2. 링크 단계에서 "unresolved external symbol" 오류가 나면:
   - 함수 이름이 `.asm`과 `.h`의 `extern "C"` 선언이 정확히 일치하는지 확인(대소문자 포함)
   - x86 빌드인데 x64용 `.asm`(또는 그 반대)이 포함 목록에 섞여 있지 않은지 확인
   - `.vcxproj`에 7.1절의 `ExtensionSettings`/`ExtensionTargets` `<Import>` 두 개가 모두 있는지 확인 — 둘 중 하나라도 빠지면 `<MASM Include="...">` 항목이 있어도 `ml.exe`/`ml64.exe`가 아예 호출되지 않아 `.obj`가 안 만들어지고, 그 결과가 "unresolved external symbol"로 나타납니다.
3. `CpuInfo86.asm`/`CpuInfo64.asm`/`PciInfo86.asm`/`PciInfo64.asm` 4개 파일 모두 내용이 준비되어 있으므로, 위 1~2단계만 정확히 따르면 `CCpuInfo`(PROCESSOR 섹션)와 `CPciInfo`(PCI 섹션) 둘 다 정상 링크됩니다.
