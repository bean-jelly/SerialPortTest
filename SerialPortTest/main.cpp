#include "serialPort.h"
#include <iostream>

std::string sendString = "AT+COPS?\r";

void MyLogCallBack(const char *file, int line, const char *func, int severity, const char *content)
{
    if (severity < 1)
    {
        return;
    }

    std::cout << "[" << func << ":" << line << "]" << content << std::endl;
}

void readCallback(const std::string &readStr)
{
    std::cout << readStr << std::endl;
}

int main()
{
    SerialPort* serialPort = new SerialPort("COM5");
    //serialPort->setLogCallback(MyLogCallBack);
    serialPort->open();
    serialPort->setReadCallback(readCallback);

    std::string input;
    while (std::cin >> input)
    {
        if (input == "q")
        {
            break;
        }
        if (serialPort->isConnected())
        {
            serialPort->write(input);
        }
        else
        {
            std::cout << "Not Connect" << std::endl;
        }
    }
    return 0;
}
