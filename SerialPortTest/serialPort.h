/*
* Author: Manash Kumar Mandal
* Modified Library introduced in Arduino Playground which does not work
* This works perfectly
* LICENSE: MIT
*/

#ifndef SERIALPORT_H
#define SERIALPORT_H

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "defines.h"
#include "SerialImpl.h"

class SerialPort
{
public:
    SerialPort(const std::string &port = "", uint32_t baudreate = 9600);

    ~SerialPort();

    //打开端口
    bool open();

    //是否连接
    bool isConnected();

    //写数据
    bool write(std::string &input);

    //设置读取回调
    void setReadCallback(const OnRead& callback);

    //获取接收字节数
    void getReadTotalSize(unsigned int &size);

    //设置日志回调
    static void setLogCallback(const MLogCallBack &callback);

private:
    bool beginThreads();
    bool endThreads();

private:
    bool        _connected;

    SerialImpl  *_pimpl;
};

#endif // serialPort_H
