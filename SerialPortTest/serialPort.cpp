#include "serialPort.h"
#include "comm.h"

void SerialPort::setLogCallback(const MLogCallBack &callback)
{
    mlog::SetMlogCallBack(callback);
}

SerialPort::SerialPort(const std::string &port, uint32_t baudrate) : _pimpl(new SerialImpl(port, baudrate))
{
}

bool SerialPort::open()
{
    if (_pimpl->open())
    {
        beginThreads();
        return true;
    }
    return false;
}

SerialPort::~SerialPort()
{
    endThreads();
    delete _pimpl;
}

void SerialPort::setReadCallback(const OnRead& callback)
{
    _pimpl->setReadCallback(callback);
}

bool SerialPort::write(std::string &input)
{
    if (input.find('\r') == std::string::npos)
    {
        input += '\r';
    }
    SendDataPacket* psdp = _pimpl->allocPacket(input.length());
    ::memcpy(psdp->data, input.c_str(), input.length());
    return _pimpl->putPacket(psdp);
}

bool SerialPort::isConnected()
{
    return _pimpl->isConnected();
}

bool SerialPort::beginThreads()
{
    return _pimpl->beginThreads();
}

bool SerialPort::endThreads()
{
    return _pimpl->endThreads();
}

void SerialPort::getReadTotalSize(unsigned int &size)
{
    return _pimpl->getReadTotalSize(size);
}
