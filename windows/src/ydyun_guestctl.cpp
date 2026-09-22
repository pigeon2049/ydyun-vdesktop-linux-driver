#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr DWORD kServiceNotFound = 0xffffffffUL;

struct ServiceSpec {
  const wchar_t* name;
  const wchar_t* description;
};

const std::map<std::wstring, std::wstring> kAllowedInf = {
    {L"qxldod.inf", L"QXL WDDM DOD display"},
    {L"viogpudo.inf", L"VirtIO GPU display"},
    {L"vioinput.inf", L"VirtIO keyboard and pointer"},
    {L"vioser.inf", L"VirtIO serial/SPICE channel"},
};

const std::vector<ServiceSpec> kRuntimeServices = {
    {L"spice-agent", L"open-source SPICE guest agent"},
};

const std::vector<ServiceSpec> kForbiddenServices = {
    {L"IceMainService", L"vendor ICE supervisor"},
    {L"IceDisplayService", L"vendor ICE display"},
    {L"IceInputService", L"vendor ICE input"},
    {L"IceSoundService", L"vendor ICE audio"},
    {L"IceTunnelService", L"vendor ICE tunnel"},
    {L"Vdservice", L"vendor account-capable agent"},
    {L"ZProcessMonitor", L"vendor process monitor"},
    {L"USBIP Client", L"vendor USB/IP service"},
};

std::wstring Lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t item) { return std::towlower(item); });
  return value;
}

void PrintError(const wchar_t* operation, DWORD error) {
  std::wcerr << operation << L" failed, Win32 error " << error << L"\n";
}

bool IsAdministrator() {
  BOOL is_admin = FALSE;
  PSID administrators = nullptr;
  SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
  if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                &administrators)) {
    return false;
  }
  const BOOL checked = CheckTokenMembership(nullptr, administrators, &is_admin);
  FreeSid(administrators);
  return checked == TRUE && is_admin == TRUE;
}

bool RunProcess(const std::wstring& command_line) {
  std::vector<wchar_t> command(command_line.begin(), command_line.end());
  command.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, nullptr, &startup, &process)) {
    PrintError(L"CreateProcess", GetLastError());
    return false;
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exit_code = ERROR_GEN_FAILURE;
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  if (exit_code != ERROR_SUCCESS) {
    std::wcerr << L"command exited with " << exit_code << L": "
               << command_line << L"\n";
    return false;
  }
  return true;
}

bool QueryServiceState(const std::wstring& name, DWORD* state) {
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!manager) {
    PrintError(L"OpenSCManager", GetLastError());
    return false;
  }
  SC_HANDLE service = OpenServiceW(manager, name.c_str(), SERVICE_QUERY_STATUS);
  if (!service) {
    const DWORD error = GetLastError();
    CloseServiceHandle(manager);
    if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
      *state = kServiceNotFound;
      return true;
    }
    PrintError(L"OpenService", error);
    return false;
  }
  SERVICE_STATUS_PROCESS status{};
  DWORD bytes = 0;
  const BOOL ok = QueryServiceStatusEx(
      service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status),
      sizeof(status), &bytes);
  if (ok) *state = status.dwCurrentState;
  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return ok == TRUE;
}

bool ChangeServiceState(const std::wstring& name, bool start) {
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!manager) return false;
  const DWORD access = SERVICE_QUERY_STATUS |
                       (start ? SERVICE_START : SERVICE_STOP);
  SC_HANDLE service = OpenServiceW(manager, name.c_str(), access);
  if (!service) {
    PrintError(L"OpenService", GetLastError());
    CloseServiceHandle(manager);
    return false;
  }
  BOOL changed = FALSE;
  if (start) {
    changed = StartServiceW(service, 0, nullptr);
    if (!changed && GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) changed = TRUE;
  } else {
    SERVICE_STATUS status{};
    changed = ControlService(service, SERVICE_CONTROL_STOP, &status);
    if (!changed && GetLastError() == ERROR_SERVICE_NOT_ACTIVE) changed = TRUE;
  }
  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return changed == TRUE;
}

struct HardwareState {
  bool qxl = false;
  bool virtio_gpu = false;
  bool virtio_serial = false;
  bool virtio_input = false;
  bool audio = false;
};

HardwareState DetectHardware() {
  HardwareState found;
  HDEVINFO devices = SetupDiGetClassDevsW(nullptr, nullptr, nullptr,
                                           DIGCF_ALLCLASSES | DIGCF_PRESENT);
  if (devices == INVALID_HANDLE_VALUE) return found;
  for (DWORD index = 0;; ++index) {
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    if (!SetupDiEnumDeviceInfo(devices, index, &info)) break;
    wchar_t buffer[4096]{};
    DWORD data_type = 0;
    DWORD required = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(
            devices, &info, SPDRP_HARDWAREID, &data_type,
            reinterpret_cast<PBYTE>(buffer), sizeof(buffer), &required)) {
      continue;
    }
    for (const wchar_t* id = buffer; *id; id += std::wcslen(id) + 1) {
      const std::wstring value = Lower(id);
      if (value.find(L"pci\\ven_1b36&dev_0100") != std::wstring::npos)
        found.qxl = true;
      if (value.find(L"pci\\ven_1af4&dev_1050") != std::wstring::npos)
        found.virtio_gpu = true;
      if (value.find(L"pci\\ven_1af4&dev_1003") != std::wstring::npos ||
          value.find(L"pci\\ven_1af4&dev_1043") != std::wstring::npos)
        found.virtio_serial = true;
      if (value.find(L"pci\\ven_1af4&dev_1005") != std::wstring::npos ||
          value.find(L"pci\\ven_1af4&dev_1052") != std::wstring::npos)
        found.virtio_input = true;
      if (value.find(L"hdaudio\\") != std::wstring::npos ||
          value.find(L"pci\\ven_8086&dev_2415") != std::wstring::npos ||
          value.find(L"pci\\ven_8086&dev_2668") != std::wstring::npos ||
          value.find(L"pci\\ven_8086&dev_293e") != std::wstring::npos ||
          value.find(L"pci\\ven_8086&dev_293f") != std::wstring::npos)
        found.audio = true;
    }
  }
  SetupDiDestroyDeviceInfoList(devices);
  return found;
}

void PrintUsage() {
  std::wcout
      << L"ydyun-guestctl - open-source Windows guest controller\n\n"
      << L"Usage:\n"
      << L"  ydyun-guestctl doctor\n"
      << L"  ydyun-guestctl install-drivers --root PATH\n"
      << L"  ydyun-guestctl start\n"
      << L"  ydyun-guestctl stop\n\n"
      << L"Only QXL/VirtIO/SPICE components are supported. Vendor ICE/ZTE\n"
      << L"drivers, services, account agents and monitors are rejected.\n";
}

std::wstring GetOption(int argc, wchar_t** argv, std::wstring_view option) {
  for (int i = 0; i + 1 < argc; ++i) {
    if (std::wstring_view(argv[i]) == option) return argv[i + 1];
  }
  return L"";
}

int Doctor() {
  bool ok = true;
  const HardwareState hardware = DetectHardware();
  std::wcout << (hardware.qxl ? L"[ok] " : L"[--] ")
             << L"QXL PCI 1b36:0100\n";
  std::wcout << (hardware.virtio_gpu ? L"[ok] " : L"[--] ")
             << L"VirtIO GPU PCI 1af4:1050\n";
  std::wcout << (hardware.virtio_serial ? L"[ok] " : L"[--] ")
             << L"VirtIO serial/SPICE channel\n";
  std::wcout << (hardware.virtio_input ? L"[ok] " : L"[--] ")
             << L"VirtIO input (optional when USB tablet/PS2 is exposed)\n";
  std::wcout << (hardware.audio ? L"[ok] " : L"[--] ")
             << L"standard HDA/AC97 audio device\n";
  ok = (hardware.qxl || hardware.virtio_gpu) && hardware.virtio_serial &&
       hardware.audio;

  for (const ServiceSpec& service : kRuntimeServices) {
    DWORD state = kServiceNotFound;
    if (!QueryServiceState(service.name, &state)) return 1;
    std::wcout << (state == SERVICE_RUNNING ? L"[ok] " : L"[--] ")
               << service.description << L": " << service.name << L"\n";
    ok = (state == SERVICE_RUNNING) && ok;
  }
  for (const ServiceSpec& service : kForbiddenServices) {
    DWORD state = kServiceNotFound;
    if (!QueryServiceState(service.name, &state)) return 1;
    if (state != kServiceNotFound) {
      std::wcerr << L"[unsafe] " << service.description << L": "
                 << service.name << L"\n";
      ok = false;
    }
  }
  return ok ? 0 : 1;
}

int InstallDrivers(const fs::path& root) {
  if (!IsAdministrator()) {
    std::wcerr << L"install-drivers requires an elevated administrator shell.\n";
    return 2;
  }
  if (!fs::is_directory(root)) {
    std::wcerr << L"driver root does not exist: " << root << L"\n";
    return 2;
  }

  std::map<std::wstring, fs::path> found;
  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;
    const std::wstring name = Lower(entry.path().filename().wstring());
    if (kAllowedInf.find(name) == kAllowedInf.end()) continue;
    const std::wstring full = Lower(entry.path().wstring());
    if (full.find(L"zte") != std::wstring::npos ||
        full.find(L"ice") != std::wstring::npos) {
      std::wcerr << L"reject vendor path: " << entry.path() << L"\n";
      return 1;
    }
    if (found.find(name) != found.end()) {
      std::wcerr << L"multiple versions of " << name
                 << L" found; point --root at one OS/architecture directory.\n";
      return 2;
    }
    found.emplace(name, entry.path());
  }

  if (found.find(L"vioser.inf") == found.end() ||
      (found.find(L"qxldod.inf") == found.end() &&
       found.find(L"viogpudo.inf") == found.end())) {
    std::wcerr << L"vioser plus qxldod or viogpudo are required.\n";
    return 2;
  }

  bool ok = true;
  for (const auto& item : found) {
    std::wcout << L"install " << kAllowedInf.at(item.first) << L": "
               << item.second << L"\n";
    const std::wstring command = L"pnputil.exe /add-driver \"" +
                                 item.second.wstring() + L"\" /install";
    ok = RunProcess(command) && ok;
  }
  return ok ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    PrintUsage();
    return 2;
  }
  const std::wstring command = argv[1];
  if (command == L"doctor") return Doctor();
  if (command == L"install-drivers") {
    const std::wstring root = GetOption(argc, argv, L"--root");
    if (root.empty()) {
      std::wcerr << L"install-drivers requires --root PATH.\n";
      return 2;
    }
    return InstallDrivers(fs::path(root));
  }
  if (command == L"start" || command == L"stop") {
    if (!IsAdministrator()) return 2;
    return ChangeServiceState(L"spice-agent", command == L"start") ? 0 : 1;
  }
  PrintUsage();
  return 2;
}
