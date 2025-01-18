#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <tchar.h>
#include <Windows.h>
#include <zip.h>
#include <experimental/filesystem>

LPWSTR serviceName, servicePath;
SERVICE_STATUS serviceStatus;
SERVICE_STATUS_HANDLE serviceStatusHandle;

std::string log_file, config_file;

int AddLogMessage(const char* str, int code)
{
    errno_t err;
    FILE* log;

    if ((err = fopen_s(&log, log_file.c_str(), "a+")) != 0)
        return 1;

    fprintf(log, "[code: %u] %s\n", code, str);
    fclose(log);
    return 0;
}

std::string GetExecutablePath()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);

    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos)
        path = path.substr(0, pos + 1);

    return path;
}

void ControlHandler(DWORD request)
{
    switch (request)
    {
    case SERVICE_CONTROL_STOP:
        AddLogMessage("Stopped", 0);
        serviceStatus.dwWin32ExitCode = 0;
        serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(serviceStatusHandle, &serviceStatus);
        return;

    case SERVICE_CONTROL_SHUTDOWN:
        AddLogMessage("Shutdown", 0);
        serviceStatus.dwWin32ExitCode = 0;
        serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(serviceStatusHandle, &serviceStatus);
        return;

    default:
        break;
    }

    SetServiceStatus(serviceStatusHandle, &serviceStatus);
}

int InstallService_()
{
    SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!hSCManager)
    {
        AddLogMessage("Error: Can't open Service Control Manager", 1);
        return 1;
    }

    SC_HANDLE hService = CreateService(
        hSCManager,
        serviceName,
        serviceName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        servicePath,
        NULL, NULL, NULL, NULL, NULL
    );

    if (!hService)
    {
        int err = GetLastError();
        switch (err) {
        case ERROR_ACCESS_DENIED:
            AddLogMessage("Error: ERROR_ACCESS_DENIED", 2);
            break;
        case ERROR_CIRCULAR_DEPENDENCY:
            AddLogMessage("Error: ERROR_CIRCULAR_DEPENDENCY", 3);
            break;
        case ERROR_DUPLICATE_SERVICE_NAME:
            AddLogMessage("Error: ERROR_DUPLICATE_SERVICE_NAME", 4);
            break;
        case ERROR_INVALID_HANDLE:
            AddLogMessage("Error: ERROR_INVALID_HANDLE", 5);
            break;
        case ERROR_INVALID_NAME:
            AddLogMessage("Error: ERROR_INVALID_NAME", 6);
            break;
        case ERROR_INVALID_PARAMETER:
            AddLogMessage("Error: ERROR_INVALID_PARAMETER", 7);
            break;
        case ERROR_INVALID_SERVICE_ACCOUNT:
            AddLogMessage("Error: ERROR_INVALID_SERVICE_ACCOUNT", 8);
            break;
        case ERROR_SERVICE_EXISTS:
            AddLogMessage("Error: ERROR_SERVICE_EXISTS", 9);
            break;
        default:
            AddLogMessage("Error: Undefined", 10);
        }
        CloseServiceHandle(hSCManager);
        return 1;
    }
    CloseServiceHandle(hService);

    CloseServiceHandle(hSCManager);
    AddLogMessage("Service installed", 0);
    return 0;
}

int RemoveService_()
{
    SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager)
    {
        AddLogMessage("Error: Can't open Service Control Manager", 1);
        return 1;
    }

    SC_HANDLE hService = OpenService(hSCManager, serviceName, SERVICE_STOP | DELETE);
    if (!hService)
    {
        AddLogMessage("Error: Can't remove service", 11);
        CloseServiceHandle(hSCManager);
        return 1;
    }

    DeleteService(hService);
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    AddLogMessage("Service removed", 0);
    return 0;
}

int StartService_()
{
    SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);

    SC_HANDLE hService = OpenService(hSCManager, serviceName, SERVICE_START);
    if (!StartService(hService, 0, NULL))
    {
        CloseServiceHandle(hSCManager);
        AddLogMessage("Error: Can't start service", 12);
        return 1;
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    AddLogMessage("Service started", 0);
    return 0;
}

int StopService_()
{
    SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager)
    {
        AddLogMessage("Error: Can't open Service Control Manager", 1);
        return 1;
    }

    SC_HANDLE hService = OpenService(hSCManager, serviceName, SERVICE_STOP);
    if (!hService)
    {
        AddLogMessage("Error: Can't stop service", 13);
        CloseServiceHandle(hSCManager);
        return 1;
    }

    ControlService(hService, SERVICE_CONTROL_STOP, &serviceStatus);
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    AddLogMessage("Service stopped", 0);
    return 0;
}

bool CheckMask(std::string path, std::string mask)
{
    int i = 0, j = 0;
    int lm = -1, lp = 0;
    while (path[i])
    {
        if (mask[j] == '*')
        {
            lp = i;
            lm = ++j;
        }
        else if (path[i] == mask[j] || mask[j] == '?')
        {
            i++;
            j++;
        }
        else if (path[i] != mask[j])
        {
            if (lm == -1)
                return false;
            i = ++lp;
            j = lm;
        }
    }
    if (!mask[j])
        return !path[i];
    return false;
}

void ServiceActivity()
{
    int error;

    std::ifstream config;
    std::string dir_path, archive_path, file_mask;
    zip_t* archive;
    zip_source_t* source;
    config.open(config_file.c_str());
    std::getline(config, dir_path);
    std::getline(config, archive_path);
    WIN32_FIND_DATAA wfd;
    HANDLE const hFind = FindFirstFileA(archive_path.c_str(), &wfd);
    if (INVALID_HANDLE_VALUE == hFind)
        archive = zip_open(archive_path.c_str(), ZIP_CREATE, &error);
    else archive = zip_open(archive_path.c_str(), 0, &error);

    while (std::getline(config, file_mask))
    {
        for (const auto& dirEntry : std::experimental::filesystem::recursive_directory_iterator(dir_path))
        {
            if (CheckMask(dirEntry.path().filename().string(), file_mask))
            {
                std::string path = dirEntry.path().string();
                path.erase(0, dir_path.size() + 1);
                if (zip_name_locate(archive, path.c_str(), NULL) == -1)
                {
                    if ((source = zip_source_file(archive, dirEntry.path().string().c_str(), 0, -1)) == NULL 
                        || zip_file_add(archive, path.c_str(), source, ZIP_FL_ENC_UTF_8) < 0)
                    {
                        zip_source_free(source);
                        AddLogMessage("Error: Can't add file to archive", 15);
                    }
                }
                else
                {
                    struct stat f_stat;
                    zip_stat_t a_stat;
                    stat(dirEntry.path().string().c_str(), &f_stat);
                    zip_stat(archive, path.c_str(), 0, &a_stat);
                    if (&f_stat.st_mtime != &a_stat.mtime)
                    {
                        if ((source = zip_source_file(archive, dirEntry.path().string().c_str(), 0, -1)) == NULL 
                            || zip_file_add(archive, path.c_str(), source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0)
                        {
                            zip_source_free(source);
                            AddLogMessage("Error: Can't update file in archive", 16);
                        }
                    }
                }
            }
        }
    }

    config.close();
    zip_close(archive);
}

void ServiceMain(int argc, char** argv)
{
    serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    serviceStatus.dwCurrentState = SERVICE_START_PENDING;
    serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    serviceStatus.dwWin32ExitCode = 0;
    serviceStatus.dwServiceSpecificExitCode = 0;
    serviceStatus.dwCheckPoint = 0;
    serviceStatus.dwWaitHint = 0;

    serviceStatusHandle = RegisterServiceCtrlHandler(serviceName, (LPHANDLER_FUNCTION)ControlHandler);
    if (serviceStatusHandle == (SERVICE_STATUS_HANDLE)0)
        return;

    AddLogMessage("Monitoring started", 0);

    serviceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(serviceStatusHandle, &serviceStatus);

    while (serviceStatus.dwCurrentState == SERVICE_RUNNING)
    {
        ServiceActivity();
        Sleep(10000);
    }
}

int _tmain(DWORD argc, _TCHAR* argv[])
{
    serviceName = LPWSTR(L"BackupService");

    wchar_t buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    servicePath = LPWSTR(buffer);

    log_file = GetExecutablePath() + "service_logs.txt";
    config_file = GetExecutablePath() + "service_config.txt";

    if (argc == 1)
    {
        SERVICE_TABLE_ENTRY ServiceTable[2];
        ServiceTable[0] = { serviceName, (LPSERVICE_MAIN_FUNCTION)ServiceMain };
        ServiceTable[1] = { NULL, NULL };

        if (!StartServiceCtrlDispatcher(ServiceTable))
            AddLogMessage("Error: StartServiceCtrlDispatcher", 14);

        return 0;
    }

    if (wcscmp(argv[argc - 1], _T("install")) == 0)
        InstallService_();
    else if (wcscmp(argv[argc - 1], _T("remove")) == 0)
        RemoveService_();
    else if (wcscmp(argv[argc - 1], _T("start")) == 0)
        StartService_();
    else if (wcscmp(argv[argc - 1], _T("stop")) == 0)
        StopService_();

    return 0;
}