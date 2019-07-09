#pragma once
#include <Windows.h>
#include <vector>
#include "lists.h"

class Overlapped : public OVERLAPPED
{
public:
    Overlapped(bool manual, bool sigaled)
    {
        Internal = 0;
        InternalHigh = 0;
        Offset = 0;
        OffsetHigh = 0;
        hEvent = ::CreateEvent(nullptr, manual, sigaled ? TRUE : FALSE, nullptr);
    }
    ~Overlapped()
    {
        ::CloseHandle(hEvent);
    }
};

class CriticalLocker
{
public:
    CriticalLocker()
    {
        ::InitializeCriticalSection(&_cs);
    }
    ~CriticalLocker()
    {
        ::DeleteCriticalSection(&_cs);
    }

    void lock()
    {
        ::EnterCriticalSection(&_cs);
    }

    void unlock()
    {
        ::LeaveCriticalSection(&_cs);
    }

    bool try_lock()
    {
        return !!::TryEnterCriticalSection(&_cs);
    }

private:
    CRITICAL_SECTION _cs;
};

// �����¼��������ӿ�
class IComEventListener
{
public:
    virtual void do_event(DWORD evt) = 0;
};

class EventEventListener : public IComEventListener
{
public:
    operator IComEventListener*() {
        return this;
    }
    virtual void do_event(DWORD evt) override
    {
        event = evt;
        ::SetEvent(hEvent);
    }

public:
    EventEventListener()
    {
        hEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    }
    ~EventEventListener()
    {
        ::CloseHandle(hEvent);
    }

    void reset()
    {
        ::ResetEvent(hEvent);
    }

public:
    DWORD	event;
    HANDLE	hEvent;
};

// �����¼��������ӿ� ������
class CComEventListener
{
    struct item {
        item(IComEventListener* p, DWORD _mask)
            : listener(p)
            , mask(_mask)
        {}

        IComEventListener* listener;
        DWORD mask;
    };
public:
    void callListeners(DWORD dwEvt) {
        _lock.lock();
        for (auto& item : _listeners) {
            if (dwEvt & item.mask) {
                item.listener->do_event(dwEvt);
            }
        }
        _lock.unlock();
    }
    void addListener(IComEventListener* pcel, DWORD mask) {
        _lock.lock();
        _listeners.push_back(item(pcel, mask));
        _lock.unlock();
    }
    void removeListener(IComEventListener* pcel) {
        _lock.lock();
        for (auto it = _listeners.begin(); it != _listeners.end(); it++) {
            if (it->listener == pcel) {
                _listeners.erase(it);
                break;
            }
        }
        _lock.unlock();
    }

protected:
    CriticalLocker	_lock;
    std::vector<item>	_listeners;
};

// Ĭ�Ϸ��ͻ�������С, �����˴�С���Զ����ڴ����
const int csdp_def_size = 1024;
enum csdp_type {
    csdp_local,		// ���ذ�, ����Ҫ�ͷ�
    csdp_alloc,		// �����, ������������Ĭ�ϻ��������ڲ�����������ʱ������
    csdp_exit,
};

#pragma warning(push)
#pragma warning(disable:4200)	// nonstandard extension used : zero-sized array in struct/union
#pragma pack(push,1)

// �����������ݰ�, ������������
struct SendDataPacket {
    csdp_type		type;			// ������
    list_s			_list_entry;	// ������ӵ����Ͷ���
    bool			used;			// �Ƿ��ѱ�ʹ��
    int				cb;				// ���ݰ����ݳ���
    unsigned char	data[0];
};

// ��չ�������ݰ�, ��һ�� csdp_def_size ��С�Ļ�����
struct SendDataPacketExtended {
    csdp_type		type;			// ������
    list_s			_list_entry;	// ������ӵ����Ͷ���
    bool			used;			// �Ƿ��ѱ�ʹ��
    int				cb;				// ���ݰ����ݳ���
    unsigned char	data[csdp_def_size];
};

#pragma pack(pop)
#pragma warning(pop)

class DataPacketManager
{
public:
    DataPacketManager();
    ~DataPacketManager();
    void					empty();
    SendDataPacket*		    alloc(int size);						// ͨ���˺�����ȡһ����������ָ����С���ݵİ�
    void					release(SendDataPacket* psdp);		// ����һ����
    void					put(SendDataPacket* psdp);			// ���Ͷ���β����һ���µ����ݰ�
    void					put_front(SendDataPacket* psdp);	// ����һ���������ݰ���������, ���ȴ���
    SendDataPacket*		    get();									// ����ȡ�ߵ��ô˽ӿ�ȡ�����ݰ�, û�а�ʱ�ᱻ����
    SendDataPacket*		    query_head();
    HANDLE					get_event() const { return _hEvent; }

private:
    SendDataPacketExtended	_data[100];	// Ԥ����ı��ذ��ĸ���
    CriticalLocker			_lock;		// ���߳���
    HANDLE					_hEvent;	// ����get()�ɲ�������, ��Ϊ�����ط�Ҫput()!
    list_s					_list;		// �������ݰ�����
};
