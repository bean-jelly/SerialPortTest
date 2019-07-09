#include "comm.h"

DataPacketManager::DataPacketManager()
    : _hEvent(0)
{
    _hEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);

    list_init(&_list);
    for (int i = 0; i < sizeof(_data) / sizeof(_data[0]); i++)
        _data[i].used = false;
}

DataPacketManager::~DataPacketManager()
{
    ::CloseHandle(_hEvent);
}

SendDataPacket* DataPacketManager::alloc(int size)
{
    _lock.lock();

    SendDataPacket* psdp = NULL;

    if (size <= csdp_def_size) {
        for (int i = 0; i < sizeof(_data) / sizeof(_data[0]); i++) {
            if (_data[i].used == false) {
                psdp = (SendDataPacket*)&_data[i];
                break;
            }
        }
        if (psdp != NULL) {
            psdp->used = true;
            psdp->type = csdp_type::csdp_local;
            psdp->cb = size;
            goto _exit;
        }
    }

    while (psdp == NULL) {
        psdp = (SendDataPacket*)new char[sizeof(SendDataPacket) + size];
    }
    psdp->type = csdp_type::csdp_alloc;
    psdp->used = true;
    psdp->cb = size;
    goto _exit;

_exit:
    _lock.unlock();
    return psdp;
}

void DataPacketManager::release(SendDataPacket* psdp)
{
    switch (psdp->type)
    {
    case csdp_type::csdp_alloc:
        delete[](char*)psdp;
        break;
    case csdp_type::csdp_local:
    case csdp_type::csdp_exit:
        _lock.lock();
        psdp->used = false;
        _lock.unlock();
        break;
    default:
        break;
    }
}

void DataPacketManager::put(SendDataPacket* psdp)
{
    _lock.lock();
    list_insert_tail(&_list, &psdp->_list_entry);
    _lock.unlock();
    ::SetEvent(_hEvent);
}

SendDataPacket* DataPacketManager::get()
{
    SendDataPacket* psdp = NULL;

    for (;;)
    {
        _lock.lock();
        list_s* pls = list_remove_head(&_list);
        _lock.unlock();

        if (pls != NULL) {
            psdp = list_data(pls, SendDataPacket, _list_entry);
            return psdp;
        }
        else {
            ::WaitForSingleObject(_hEvent, INFINITE);
        }
    }
}

void DataPacketManager::put_front(SendDataPacket* psdp)
{
    _lock.lock();
    list_insert_head(&_list, &psdp->_list_entry);
    _lock.unlock();
    ::SetEvent(_hEvent);
}

void DataPacketManager::empty()
{
    while (!list_is_empty(&_list)) {
        list_s* p = list_remove_head(&_list);
        SendDataPacket* psdp = list_data(p, SendDataPacket, _list_entry);
        release(psdp);
    }
}

SendDataPacket* DataPacketManager::query_head()
{
    SendDataPacket* psdp = NULL;
    _lock.lock();
    if (list_is_empty(&_list)) {
        psdp = NULL;
    }
    else {
        psdp = list_data(_list.next, SendDataPacket, _list_entry);
    }
    _lock.unlock();
    return psdp;
}
