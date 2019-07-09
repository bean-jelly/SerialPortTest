#pragma once
#ifndef SERIAL_IMPL_H
#define SERIAL_IMPL_H

#include <windows.h>
#include "defines.h"
#include "threadpool.h"
#include "comm.h"

class SerialImpl
{
public:
    SerialImpl(const std::string &port = "", uint32_t baudreate = 9600);

    ~SerialImpl();

    bool open();

    bool isConnected();

    bool putPacket(SendDataPacket* psdp, bool bfront = false, bool bsilent = false);

    SendDataPacket*	allocPacket(int size);

    void setReadCallback(const OnRead& callback);

    void getReadTotalSize(unsigned int &size);

public:
    bool beginThreads();
    bool endThreads();

private:
    bool _beginThreads();
    void _endThreads();

    int threadEvent();
    int threadWrite();
    int threadRead();
    static unsigned int __stdcall threadHelper(void* pv);

private:
    void callDataReceivers(unsigned char* input, int cb);
    void releasePacket(SendDataPacket* psdp) { _sendData.release(psdp); }
    void emptyPacketList() { _sendData.empty(); }

private:
    COMMTIMEOUTS                _timeout;
    unsigned long               _baudrate;
    std::string                 _port;
    bool                        _connected;

private:
    struct thread_state {
        HANDLE hThread;
        HANDLE hEventToBegin;
        HANDLE hEventToExit;
    };

    HANDLE                      _handler;
    CriticalLocker			    _dataReceiverLock;
    OnRead                      _readCallback;
    thread_state	            _threadRead;
    thread_state	            _threadWrite;
    thread_state	            _threadEvent;
    CComEventListener	        _eventListener;
    DataPacketManager	        _sendData;

    struct threadHelperContext
    {
        SerialImpl* that;
        enum class e_which {
            kEvent,
            kRead,
            kWrite,
        };
        e_which which;
    };

    ThreadPool*                 _pool;
    std::string                 _readLine;
    unsigned int                _readTotalSize;
};

#endif
