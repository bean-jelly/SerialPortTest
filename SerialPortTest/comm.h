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

// 串口事件监听器接口
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

// 串口事件监听器接口 管理器
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

// 默认发送缓冲区大小, 超过此大小会自动从内存分配
const int csdp_def_size = 1024;
enum csdp_type {
    csdp_local,		// 本地包, 不需要释放
    csdp_alloc,		// 分配包, 在数据量大于默认缓冲区或内部缓冲区不够时被分配
    csdp_exit,
};

#pragma warning(push)
#pragma warning(disable:4200)	// nonstandard extension used : zero-sized array in struct/union
#pragma pack(push,1)

// 基础发送数据包, 不包含缓冲区
struct SendDataPacket {
    csdp_type		type;			// 包类型
    list_s			_list_entry;	// 用于添加到发送队列
    bool			used;			// 是否已被使用
    int				cb;				// 数据包数据长度
    unsigned char	data[0];
};

// 扩展发送数据包, 有一个 csdp_def_size 大小的缓冲区
struct SendDataPacketExtended {
    csdp_type		type;			// 包类型
    list_s			_list_entry;	// 用于添加到发送队列
    bool			used;			// 是否已被使用
    int				cb;				// 数据包数据长度
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
    SendDataPacket*		    alloc(int size);						// 通过此函数获取一个可以容纳指定大小数据的包
    void					release(SendDataPacket* psdp);		// 返还一个包
    void					put(SendDataPacket* psdp);			// 向发送队列尾插入一个新的数据包
    void					put_front(SendDataPacket* psdp);	// 插入一个发送数据包到队列首, 优先处理
    SendDataPacket*		    get();									// 包获取者调用此接口取得数据包, 没有包时会被挂起
    SendDataPacket*		    query_head();
    HANDLE					get_event() const { return _hEvent; }

private:
    SendDataPacketExtended	_data[100];	// 预定义的本地包的个数
    CriticalLocker			_lock;		// 多线程锁
    HANDLE					_hEvent;	// 尝试get()可不能锁定, 因为其它地方要put()!
    list_s					_list;		// 发送数据包队列
};
