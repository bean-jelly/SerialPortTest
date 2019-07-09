#pragma once

typedef struct _list_s {
    struct _list_s* prior;
    struct _list_s* next;
}list_s;

#define list_data(addr,type,member) \
	((type*)(((unsigned char*)addr)-(unsigned long)&(((type*)0)->member)))

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

    int   list_is_empty(list_s* phead);
    void  list_init(list_s* phead);
    void  list_insert_head(list_s* phead, list_s* plist);
    void  list_insert_tail(list_s* phead, list_s* plist);
    list_s* list_remove_head(list_s* phead);
    list_s* list_remove_tail(list_s* phead);
    int list_remove(list_s* phead, list_s* p);

#ifdef __cplusplus
}
#endif // __cplusplus
