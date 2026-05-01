#pragma once

#include <string>

#ifdef AIM_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Axiom {

    class SingleInstance
    {
    public:
        explicit SingleInstance(const std::string& mutexName)
        {
#ifdef AIM_PLATFORM_WINDOWS
            m_Handle = CreateMutexA(nullptr, TRUE, mutexName.c_str());
            if (m_Handle && GetLastError() == ERROR_ALREADY_EXISTS)
                m_AlreadyRunning = true;
#else
            const std::string path = "/tmp/" + mutexName + ".lock";
            m_FileDescriptor = open(path.c_str(), O_CREAT | O_RDWR, 0666);
            if (m_FileDescriptor != -1)
                m_AlreadyRunning = (flock(m_FileDescriptor, LOCK_EX | LOCK_NB) != 0);
#endif
        }

        ~SingleInstance()
        {
#ifdef AIM_PLATFORM_WINDOWS
            if (m_Handle)
            {
                ReleaseMutex(m_Handle);
                CloseHandle(m_Handle);
            }
#else
            if (m_FileDescriptor != -1)
            {
                flock(m_FileDescriptor, LOCK_UN);
                close(m_FileDescriptor);
            }
#endif
        }

        bool IsAlreadyRunning() const { return m_AlreadyRunning; }

    private:
#ifdef AIM_PLATFORM_WINDOWS
        HANDLE m_Handle = nullptr;
#else
        int m_FileDescriptor = -1;
#endif
        bool m_AlreadyRunning = false;
    };
}