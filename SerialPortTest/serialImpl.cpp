#include "SerialImpl.h"
#include <process.h>

inline std::string  _prefix_port_if_needed(const std::string &input)
{
    static std::string  windows_com_port_prefix = "\\\\.\\";
    if (input.compare(windows_com_port_prefix) != 0)
    {
        return windows_com_port_prefix + input;
    }
    return input;
}

SerialImpl::SerialImpl(const std::string &port, uint32_t baudrate)
{
    _readTotalSize = 0;

    _port = port;
    _baudrate = baudrate;
    _timeout.ReadIntervalTimeout = MAXDWORD;
    _timeout.ReadTotalTimeoutMultiplier = 0;
    _timeout.ReadTotalTimeoutConstant = 0;
    _timeout.WriteTotalTimeoutMultiplier = 0;
    _timeout.WriteTotalTimeoutConstant = 0;

    _beginThreads();

    _pool = new ThreadPool(4);
}

SerialImpl::~SerialImpl()
{
    _endThreads();

    if (this->_connected)
    {
        this->_connected = false;
        CloseHandle(this->_handler);
    }
}

unsigned int __stdcall SerialImpl::threadHelper(void* pv)
{
    auto pctx = reinterpret_cast<threadHelperContext*>(pv);
    auto comm = pctx->that;
    auto which = pctx->which;

    delete pctx;

    switch (which)
    {
    case threadHelperContext::e_which::kEvent:
        return comm->threadEvent();
    case threadHelperContext::e_which::kRead:
        return comm->threadRead();
    case threadHelperContext::e_which::kWrite:
        return comm->threadWrite();
    default:
        return 1;
    }
}

bool SerialImpl::_beginThreads()
{
    threadHelperContext* pctx = nullptr;

    // 开启读线程
    _threadRead.hEventToBegin = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    _threadRead.hEventToExit = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);

    pctx = new threadHelperContext;
    pctx->that = this;
    pctx->which = threadHelperContext::e_which::kRead;
    _threadRead.hThread = (HANDLE)::_beginthreadex(nullptr, 0, threadHelper, pctx, 0, nullptr);

    if (!_threadRead.hEventToBegin || !_threadRead.hEventToExit || !_threadRead.hThread)
    {
        LOG_ERROR << "应用程序初始化失败, 即将退出!";
        ::exit(1);
    }

    // 开启写线程
    _threadWrite.hEventToBegin = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    _threadWrite.hEventToExit = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);

    pctx = new threadHelperContext;
    pctx->that = this;
    pctx->which = threadHelperContext::e_which::kWrite;
    _threadWrite.hThread = (HANDLE)::_beginthreadex(nullptr, 0, threadHelper, pctx, 0, nullptr);

    if (!_threadWrite.hEventToBegin || !_threadWrite.hEventToExit || !_threadWrite.hThread)
    {
        LOG_ERROR << "应用程序初始化失败, 即将退出!";
        ::exit(1);
    }

    // 开启事件线程
    _threadEvent.hEventToBegin = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    _threadEvent.hEventToExit = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);

    pctx = new threadHelperContext;
    pctx->that = this;
    pctx->which = threadHelperContext::e_which::kEvent;
    _threadEvent.hThread = (HANDLE)::_beginthreadex(nullptr, 0, threadHelper, pctx, 0, nullptr);

    if (!_threadEvent.hEventToBegin || !_threadEvent.hEventToExit || !_threadEvent.hThread)
    {
        LOG_ERROR << "应用程序初始化失败, 即将退出!";
        ::exit(1);
    }

    return true;
}

void SerialImpl::_endThreads()
{
    // 由线程在退出之前设置并让当前线程等待他们的结束
    ::ResetEvent(_threadRead.hEventToExit);
    ::ResetEvent(_threadWrite.hEventToExit);
    ::ResetEvent(_threadEvent.hEventToExit);

    // 此时串口是关闭的, 收到此事件即准备退出线程
    ::SetEvent(_threadRead.hEventToBegin);
    ::SetEvent(_threadWrite.hEventToBegin);
    ::SetEvent(_threadEvent.hEventToBegin);

    // 等待线程完全退出
    ::WaitForSingleObject(_threadRead.hEventToExit, INFINITE);
    ::WaitForSingleObject(_threadWrite.hEventToExit, INFINITE);
    ::WaitForSingleObject(_threadEvent.hEventToExit, INFINITE);

    ::CloseHandle(_threadRead.hEventToBegin);
    ::CloseHandle(_threadRead.hEventToExit);
    ::CloseHandle(_threadWrite.hEventToBegin);
    ::CloseHandle(_threadWrite.hEventToExit);
    ::CloseHandle(_threadEvent.hEventToBegin);
    ::CloseHandle(_threadEvent.hEventToExit);

    ::CloseHandle(_threadRead.hThread);
    ::CloseHandle(_threadWrite.hThread);
    ::CloseHandle(_threadEvent.hThread);
}

int SerialImpl::threadEvent()
{
_wait_for_work:
    DWORD dw2;
    DWORD dw = ::WaitForSingleObject(_threadEvent.hEventToBegin, INFINITE);
    if (!_connected)
    {
        ::SetEvent(_threadEvent.hEventToExit);
        return 0;
    }
    Overlapped o(true, false);

_wait_again:
    DWORD dwEvent = 0;
    ::ResetEvent(o.hEvent);
    dw = ::WaitCommEvent(_handler, &dwEvent, &o);
    if (dw != FALSE)
    {
        _eventListener.callListeners(dwEvent);
        goto _wait_again;
    }
    else
    {
        if (::GetLastError() == ERROR_IO_PENDING)
        {
            HANDLE handles[2];
            handles[0] = _threadEvent.hEventToExit;
            handles[1] = o.hEvent;

            switch (::WaitForMultipleObjects(_countof(handles), handles, FALSE, INFINITE))
            {
            case WAIT_FAILED:
                LOG_INFO << "[事件线程::Wait失败]";
                goto _restart;
                break;
            case WAIT_OBJECT_0 + 0:
                LOG_INFO << "[事件线程] 收到退出事件!";
                goto _restart;
                break;
            case WAIT_OBJECT_0 + 1:
                BOOL bRet = ::GetOverlappedResult(_handler, &o, &dw2, FALSE);
                if (bRet == FALSE)
                {
                    LOG_INFO << "[事件线程::Wait失败]";
                    goto _restart;
                }
                else
                {
                    _eventListener.callListeners(dwEvent);
                    goto _wait_again;
                }
                break;
            }
        }
        else
        {
            LOG_INFO << "[事件线程]::GetLastError() != ERROR_IO_PENDING";
        }
    }
_restart:
    if (!::CancelIo(_handler)) {}

    ::WaitForSingleObject(_threadEvent.hEventToExit, INFINITE);
    ::ResetEvent(_threadEvent.hEventToExit);

    goto _wait_for_work;
}

int SerialImpl::threadWrite()
{
    EventEventListener listener;

_wait_for_work:
    DWORD dw = ::WaitForSingleObject(_threadWrite.hEventToBegin, INFINITE);
    if (!_connected)
    {
        ::SetEvent(_threadWrite.hEventToExit);
        return 0;
    }
    Overlapped overlap(false, false);
    _eventListener.addListener(listener, EV_TXEMPTY);

_get_packet:
    SendDataPacket* psdp = _sendData.get();
    if (psdp->type == csdp_type::csdp_alloc || psdp->type == csdp_type::csdp_local)
    {
        LOG_INFO << "[写线程] 取得一个发送数据包, 长度为 " << psdp->cb << " 字节";
        DWORD	nWritten = 0;
        int		nWrittenData;
        for (nWrittenData = 0; nWrittenData < psdp->cb;)
        {
            DWORD bRet = ::WriteFile(_handler, &psdp->data[0] + nWrittenData, psdp->cb - nWrittenData, NULL, &overlap);
            if (bRet != FALSE)
            {
                bRet = ::GetOverlappedResult(_handler, &overlap, &nWritten, FALSE);
                if (bRet)
                {
                    LOG_INFO << "[写线程] I/O completed immediately bytes : " << nWritten;
                }
                else
                {
                    LOG_INFO << "[写线程] GetOverlappedResult失败(I/O completed)!";
                    goto _restart;
                }
            }
            else
            {
                if (::GetLastError() == ERROR_IO_PENDING)
                {
                    HANDLE handles[2];
                    handles[0] = _threadWrite.hEventToExit;
                    handles[1] = listener.hEvent;

                    switch (::WaitForMultipleObjects(_countof(handles), &handles[0], FALSE, INFINITE))
                    {
                    case WAIT_FAILED:
                        LOG_INFO << "[写线程] Wait失败!";
                        goto _restart;
                        break;
                    case WAIT_OBJECT_0 + 0:
                        LOG_INFO << "[写线程] 收到退出事件!";
                        goto _restart;
                        break;
                    case WAIT_OBJECT_0 + 1:
                        bRet = ::GetOverlappedResult(_handler, &overlap, &nWritten, FALSE);
                        if (bRet)
                        {
                            LOG_INFO << "[写线程] 写入 " << nWritten << "个字节!";
                        }
                        else
                        {
                            LOG_INFO << "[写线程] GetOverlappedResult失败(I/O pending)!";
                            goto _restart;
                        }
                        break;
                    }
                }
                else
                {
                    LOG_INFO << "[写线程] ::GetLastError() != ERROR_IO_PENDING";
                    goto _restart;
                }
            }
            nWrittenData += nWritten;
        }
        _sendData.release(psdp);
        goto _get_packet;
    }
    else if (psdp->type == csdp_type::csdp_exit)
    {
        LOG_INFO << "[写线程] 收到退出事件!";
        _sendData.release(psdp);
        goto _restart;
    }
_restart:
    if (!::CancelIo(_handler)) {}

    _eventListener.removeListener(listener);
    listener.reset();

    ::WaitForSingleObject(_threadWrite.hEventToExit, INFINITE);
    ::ResetEvent(_threadWrite.hEventToExit);

    goto _wait_for_work;
}

int SerialImpl::threadRead()
{
    EventEventListener listener;

    const int kReadBufSize = 1 << 20;
    unsigned char* block_data = NULL;
    block_data = new unsigned char[kReadBufSize];

_wait_for_work:
    DWORD dw = ::WaitForSingleObject(_threadRead.hEventToBegin, INFINITE);
    if (!_connected)
    {
        delete[] block_data;
        ::SetEvent(_threadRead.hEventToExit);
        return 0;
    }

    Overlapped overlap(false, false);

    _eventListener.addListener(listener, EV_RXCHAR);

    HANDLE handles[2];
    handles[0] = _threadRead.hEventToExit;
    handles[1] = listener.hEvent;

_get_packet:
    switch (::WaitForMultipleObjects(_countof(handles), handles, FALSE, INFINITE))
    {
    case WAIT_FAILED:
        LOG_INFO << "[读线程] Wait失败!";
        goto _restart;
    case WAIT_OBJECT_0 + 0:
        LOG_INFO << "[读线程] 收到退出事件!";
        goto _restart;
    case WAIT_OBJECT_0 + 1:
        break;
    }

    DWORD nBytesToRead, nRead, nTotalRead;
    DWORD	comerr;
    COMSTAT	comsta;

    if (!::ClearCommError(_handler, &comerr, &comsta))
    {
        LOG_INFO << "ClearCommError()";
        goto _restart;
    }

    nBytesToRead = comsta.cbInQue;
    if (nBytesToRead == 0)
    {
        nBytesToRead++;
    }

    if (nBytesToRead > kReadBufSize)
    {
        nBytesToRead = kReadBufSize;
    }

    for (nTotalRead = 0; nTotalRead < nBytesToRead;)
    {
        BOOL bRet = ::ReadFile(_handler, block_data + nTotalRead, nBytesToRead - nTotalRead, &nRead, &overlap);
        if (bRet != FALSE)
        {
            bRet = ::GetOverlappedResult(_handler, &overlap, &nRead, FALSE);
            if (bRet)
            {
                LOG_INFO << "[读线程] 读取 " << nRead << " 字节, bRet==TRUE, nBytesToRead:" << nBytesToRead;
            }
            else
            {
                LOG_INFO << "[写线程] GetOverlappedResult失败!";
                goto _restart;
            }
        }
        else
        {
            if (::GetLastError() == ERROR_IO_PENDING)
            {
                HANDLE handles[2];
                handles[0] = _threadRead.hEventToExit;
                handles[1] = overlap.hEvent;

                switch (::WaitForMultipleObjects(_countof(handles), &handles[0], FALSE, INFINITE))
                {
                case WAIT_FAILED:
                    LOG_INFO << "[读线程] 等待失败!";
                    goto _restart;
                case WAIT_OBJECT_0 + 0:
                    LOG_INFO << "[读线程] 收到退出事件!";
                    goto _restart;
                case WAIT_OBJECT_0 + 1:
                    bRet = ::GetOverlappedResult(_handler, &overlap, &nRead, FALSE);
                    if (bRet) {
                        LOG_INFO << "[读线程] 读取 " << nRead << " 字节, bRet==FALSE";
                    }
                    else {
                        LOG_INFO << "[读线程] GetOverlappedResult失败!";
                        goto _restart;
                    }
                    break;
                }
            }
            else
            {
                LOG_INFO << "[读线程] ::GetLastError() != ERROR_IO_PENDING";
                goto _restart;
            }
        }

        if (nRead > 0)
        {
            nTotalRead += nRead;
        }
        else {
            nBytesToRead--;
        }
    }

    _readTotalSize += nBytesToRead;

    callDataReceivers(block_data, nBytesToRead);

    goto _get_packet;

_restart:
    if (!::CancelIo(_handler))
    {
    }
    ::WaitForSingleObject(_threadRead.hEventToExit, INFINITE);
    ::ResetEvent(_threadRead.hEventToExit);

    goto _wait_for_work;
}

bool SerialImpl::beginThreads()
{
    ::ResetEvent(_threadRead.hEventToExit);
    ::ResetEvent(_threadWrite.hEventToExit);
    ::ResetEvent(_threadEvent.hEventToExit);

    ::SetEvent(_threadRead.hEventToBegin);
    ::SetEvent(_threadWrite.hEventToBegin);
    ::SetEvent(_threadEvent.hEventToBegin);

    return true;
}

bool SerialImpl::endThreads()
{
    ::ResetEvent(_threadRead.hEventToBegin);
    ::ResetEvent(_threadWrite.hEventToBegin);
    ::ResetEvent(_threadEvent.hEventToBegin);

    ::SetEvent(_threadRead.hEventToExit);
    ::SetEvent(_threadWrite.hEventToExit);
    ::SetEvent(_threadEvent.hEventToExit);

    // 在读写线程退出之前, 两个end均为激发状态
    // 必须等到两个线程均退出工作状态才能有其它操作
    while (::WaitForSingleObject(_threadRead.hEventToExit, 0) == WAIT_OBJECT_0);
    while (::WaitForSingleObject(_threadWrite.hEventToExit, 0) == WAIT_OBJECT_0);
    while (::WaitForSingleObject(_threadEvent.hEventToExit, 0) == WAIT_OBJECT_0);

    return true;
}

bool SerialImpl::open()
{
    if (_port.empty())
    {
        LOG_INFO << " SerialImpl::open port empty";
        return false;
    }
    if (_connected == true)
    {
        return false;
    }

    std::string port_prefix = _prefix_port_if_needed(_port);
    this->_handler = CreateFileA(static_cast<LPCSTR>(port_prefix.c_str()),
                                 GENERIC_READ | GENERIC_WRITE,
                                 0,
                                 NULL,
                                 OPEN_EXISTING,
                                 FILE_FLAG_OVERLAPPED,
                                 NULL);
    if (this->_handler == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
        {
            LOG_INFO << "ERROR: Handle was not attached. Reason: " << _port << " not available";
        }
        return false;
    }
    else
    {
        DCB dcbSerialParameters = { 0 };

        if (!GetCommState(this->_handler, &dcbSerialParameters))
        {
            LOG_INFO << "failed to get current serial parameters";
        }
        else
        {
            dcbSerialParameters.BaudRate = _baudrate;
            dcbSerialParameters.ByteSize = 8;
            dcbSerialParameters.StopBits = ONESTOPBIT;
            dcbSerialParameters.Parity = NOPARITY;
            dcbSerialParameters.fDtrControl = DTR_CONTROL_ENABLE;

            if (!SetCommState(_handler, &dcbSerialParameters))
            {
                LOG_INFO << "ALERT: could not set Serial port parameters";
                CloseHandle(_handler);
                return false;
            }
            if (!SetCommMask(_handler, EV_RXCHAR | EV_RXFLAG | EV_TXEMPTY | EV_CTS | EV_DSR | EV_RLSD | EV_BREAK | EV_ERR | EV_RING | EV_PERR | EV_RX80FULL))
            {
                CloseHandle(_handler);
                return false;
            }
            if (!SetCommTimeouts(_handler, &_timeout)) {
                return false;
            }
            this->_connected = true;
            PurgeComm(this->_handler, PURGE_RXCLEAR | PURGE_TXCLEAR);
            PurgeComm(this->_handler, PURGE_RXCLEAR | PURGE_TXCLEAR);
        }
    }
    return true;
}

bool SerialImpl::isConnected()
{
    DWORD	comerr;
    COMSTAT	comsta;
    if (!ClearCommError(this->_handler, &comerr, &comsta))
        this->_connected = false;

    return this->_connected;
}

bool SerialImpl::putPacket(SendDataPacket* psdp, bool bfront, bool bsilent)
{
    if (_connected)
    {
        if (bfront)
        {
            _sendData.put_front(psdp);
        }
        else
        {
            _sendData.put(psdp);
        }
        return true;
    }
    else
    {
        releasePacket(psdp);
    }
    return false;
}

SendDataPacket*	SerialImpl::allocPacket(int size)
{
    return _sendData.alloc(size);
}

void SerialImpl::setReadCallback(const OnRead& callback)
{
    _readCallback = callback;
}

void SerialImpl::callDataReceivers(unsigned char* input, int cb)
{
    _dataReceiverLock.lock();

    for (int i = 0; i < cb; i++)
    {
        if (input[i] == '\r' || input[i] == '\n')
        {
            if (!_readLine.empty() && _readCallback)
            {
                std::string tmp = _readLine;
                _pool->Commit([=]() {
                    _readCallback(tmp);
                });
                _readLine.clear();
            }
        }
        else
        {
            _readLine.push_back(input[i]);
        }
    }

    _dataReceiverLock.unlock();
}

void SerialImpl::getReadTotalSize(unsigned int &size)
{
    size = _readTotalSize;
}
