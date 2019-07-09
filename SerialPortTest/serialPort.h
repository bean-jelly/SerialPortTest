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

    //�򿪶˿�
    bool open();

    //�Ƿ�����
    bool isConnected();

    //д����
    bool write(std::string &input);

    //���ö�ȡ�ص�
    void setReadCallback(const OnRead& callback);

    //��ȡ�����ֽ���
    void getReadTotalSize(unsigned int &size);

    //������־�ص�
    static void setLogCallback(const MLogCallBack &callback);

private:
    bool beginThreads();
    bool endThreads();

private:
    bool        _connected;

    SerialImpl  *_pimpl;
};

#endif // serialPort_H
