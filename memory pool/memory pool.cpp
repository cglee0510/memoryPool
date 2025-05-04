// memory pool.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include <iostream>
#include <Windows.h>
#include <vector>
#include <list>
using namespace std;

struct sCSLock
{
    CRITICAL_SECTION  cs;

    sCSLock() { InitializeCriticalSection(&cs); }
    ~sCSLock() { DeleteCriticalSection(&cs); }

    void Lock() { EnterCriticalSection(&cs); }
    void Unlock() { LeaveCriticalSection(&cs); }
};

// 이미 만들어진 cs를 가지고 사용.
// InitializeCriticalSection 프로그램 전체에 한번만
struct CSScopeLock
{
    CRITICAL_SECTION& m_cs;

public:
    CSScopeLock(CRITICAL_SECTION& cs)
        : m_cs(cs)
    {
        ::EnterCriticalSection(&m_cs);
    }

    ~CSScopeLock()
    {
        ::LeaveCriticalSection(&m_cs);
    }

    operator bool()
    {
        return true;
    }

private:
    // 대입과 복사 생성자로 생성 방지
    CSScopeLock& operator=(CSScopeLock& rval);
    CSScopeLock(const CRITICAL_SECTION& cs);
};

struct sHeapBlockDump
{
    void* pAddress;
    size_t nSize;
    DWORD dwLine;

    char File[255];

    sHeapBlockDump()
    {
        pAddress = NULL;
        nSize = 0;
        dwLine = 0;
        memset(File, NULL, sizeof(char) * 255);
    }
};

//#define _RESOURCE_POOL_DUMP

#define _USING_HEAP_BLOCK_DATA          0xFF
#define _DELETE_DUPLIICATION_CHECK

class cResourcePoolManager
{
public:
    cResourcePoolManager(size_t BlockSize, size_t BlockCount, bool bDestroyFlag)
        : m_nHeapBlockSize(BlockSize), m_nHeapBlockCount(BlockCount), m_bDestroyPtr(bDestroyFlag), m_bIsNewAllocate(false)
    {
        if (BlockSize < sizeof(void*))
            m_nHeapBlockSize = sizeof(void*);

        m_vFreeList.reserve(BlockCount);
    }

    ~cResourcePoolManager()
    {
#ifdef _DEBUG
#ifdef _RESOURCE_POOL_DUMP
        DumpHeapBlockList();
#endif
#endif
        Uninitialize();
    }

    // 디버깅(누수보고 오류) 편의 위해 명시적 호출을 위한 함수.
    void Uninitialize()
    {
        if (m_bDestroyPtr)
        {
            for (auto& block : m_lHeapBlockList)
            {
                delete[] block;
            }
            m_lHeapBlockList.clear();

            m_vFreeList.clear();
            vector<void*>().swap(m_vFreeList);

            m_bDestroyPtr = false;
        }
    }

    void* HeapAlloc()
    {
        void* pPTR = NULL;

        if (!m_vFreeList.empty())
        {
            m_bIsNewAllocate = false;

            pPTR = m_vFreeList.back();
            m_vFreeList.pop_back();

            return pPTR;
        }
        else
        {
            m_bIsNewAllocate = true;

            BYTE* pBlock = new BYTE[m_nHeapBlockSize * m_nHeapBlockCount];
            if (pBlock == NULL)
                return NULL;

            m_lHeapBlockList.push_back(pBlock);

            for (size_t i = 0; i < m_nHeapBlockCount; ++i)
            {
                m_vFreeList.push_back(pBlock + i * m_nHeapBlockSize);
            }

            pPTR = m_vFreeList.back();
            m_vFreeList.pop_back();

            return pPTR;
        }
    }

    void HeapFree(void* pPTR)
    {
#ifdef _DELETE_DUPLIICATION_CHECK
        for (const auto& ptr : m_vFreeList)
        {
            if (ptr == pPTR)
            {
                char szBuff[128];
                sprintf_s(szBuff, sizeof(szBuff), "%s %s 메모리중복 삭제 호출됨\r\n", __FILE__, __FUNCTION__);
                OutputDebugStringA(szBuff);
            }
        }
#endif
        m_vFreeList.push_back(pPTR);
    }

#ifdef _DEBUG
    void AddDump(void* pAddr, const char* pName, DWORD LineNum)
    {
        if (m_bIsNewAllocate == false)
            return;

        sHeapBlockDump dump;
        dump.pAddress = pAddr;
        dump.nSize = (m_nHeapBlockSize * m_nHeapBlockCount);
        strcpy_s(dump.File, sizeof(dump.File), pName);
        dump.dwLine = LineNum;

        m_vDumpList.push_back(dump);
    }

    void DumpHeapBlockList()
    {
        size_t totalSize = 0;
        char buf[1024];

        for (const auto& dump : m_vDumpList)
        {
            sprintf_s(buf, sizeof(buf), "%-50s:\t\tLINE %d,\t\tADDRESS Ox%p\t%d  Bytes Using\n", dump.File, dump.dwLine, dump.pAddress, dump.nSize);
            OutputDebugStringA(buf);
            totalSize += dump.nSize;
        }

        sprintf_s(buf, sizeof(buf), "-----------------------------------------------------------\n");
        OutputDebugStringA(buf);
        sprintf_s(buf, sizeof(buf), "Total HeapBlock: %d Bytes\n", totalSize);
        OutputDebugStringA(buf);
        sprintf_s(buf, sizeof(buf), "DELETE_OPTION :  %d \n", m_bDestroyPtr);
        OutputDebugStringA(buf);
        sprintf_s(buf, sizeof(buf), "-----------------------------------------------------------\n");
    }
#endif

private:
    size_t m_nHeapBlockSize;
    size_t m_nHeapBlockCount;

    bool m_bDestroyPtr, m_bIsNewAllocate;

    vector<void*> m_vFreeList;
    list<BYTE*> m_lHeapBlockList;

#ifdef _DEBUG
    vector<sHeapBlockDump> m_vDumpList;
#endif
};

template <typename T, size_t BlockSize = 30, bool DELETE_OPTION = true>
class cResourcePoolUnLock
{
    static cResourcePoolManager ms_ResourcePoolManager;

public:
#ifdef _DEBUG
#ifdef _RESOURCE_POOL_DUMP
    void* __cdecl operator new(size_t BlockSize, const char* pFile, int Line)
    {
        void* pBlock = ms_ResourcePoolManager.HeapAlloc();
        ms_ResourcePoolManager.AddDump(pBlock, pFile, Line);
        return pBlock;
    }
#endif
#endif

    void* __cdecl operator new(size_t BlockSize)
    {
        return ms_ResourcePoolManager.HeapAlloc();
    }

    void* __cdecl operator new(size_t BlockSize, int, char*, int)
    {
        return ms_ResourcePoolManager.HeapAlloc();
    }

    void __cdecl operator delete(void* pPtr)
    {
        ms_ResourcePoolManager.HeapFree(pPtr);
    }

    void __cdecl operator delete(void* pPtr, int, char*, int)
    {
        ms_ResourcePoolManager.HeapFree(pPtr);
    }

    static cResourcePoolManager* GetPoolManager() { return &(ms_ResourcePoolManager); }

    static void Uninitialize()
    {
        ms_ResourcePoolManager.Uninitialize();
    }
};

template <typename T, size_t BlockSize, bool DELETE_OPTION>
cResourcePoolManager cResourcePoolUnLock<T, BlockSize, DELETE_OPTION>::ms_ResourcePoolManager(sizeof(T), BlockSize, DELETE_OPTION);

template <typename T, size_t BlockSize>
inline void* operator new(size_t size, cResourcePoolUnLock<T, BlockSize>& pool)
{
    return pool.GetPoolManager()->HeapAlloc();
}

template <typename T, size_t BlockSize>
inline void Del(T* ptr, cResourcePoolUnLock<T, BlockSize>& pool)
{
    ptr->~T();
    pool.GetPoolManager()->HeapFree(ptr);
}

// class public 내부와 클래스 외부에 선언해줄것
#define Inner_Decl_ResourcePoolUnLock(ClassType, Size) \
    static cResourcePoolUnLock<##ClassType, ##Size> ms_kPool##ClassType; \
public: \
    void operator delete(void* pPtr) \
    { ms_kPool##ClassType.GetPoolManager()->HeapFree(pPtr); } \
    \
    void* operator new(size_t n) \
    { return ms_kPool##ClassType.GetPoolManager()->HeapAlloc(); }

#define Outer_Decl_ResourcePoolUnLock(ClassType, Size) \
    cResourcePoolUnLock<##ClassType, ##Size> ##ClassType::ms_kPool##ClassType;

template <typename T, size_t BlockSize = 30, bool DELETE_OPTION = true>
class cResourcePoolLock
{
    static cResourcePoolManager ms_ResourcePoolManager;
    static sCSLock ms_csLock;

public:
#ifdef _DEBUG
#ifdef _RESOURCE_POOL_DUMP
    void* __cdecl operator new(size_t BlockSize, const char* pFile, int Line)
    {
        ms_csLock.Lock();
        void* pBlock = ms_ResourcePoolManager.HeapAlloc();
        ms_ResourcePoolManager.AddDump(pBlock, pFile, Line);
        ms_csLock.Unlock();
        return pBlock;
    }
#endif
#endif

    void* __cdecl operator new(size_t BlockSize)
    {
        ms_csLock.Lock();
        void* pBlock = ms_ResourcePoolManager.HeapAlloc();
        ms_csLock.Unlock();
        return pBlock;
    }

    void* __cdecl operator new(size_t BlockSize, int, char*, int)
    {
        ms_csLock.Lock();
        void* pBlock = ms_ResourcePoolManager.HeapAlloc();
        ms_csLock.Unlock();
        return pBlock;
    }

    void __cdecl operator delete(void* pPtr)
    {
        ms_csLock.Lock();
        ms_ResourcePoolManager.HeapFree(pPtr);
        ms_csLock.Unlock();
    }

    void __cdecl operator delete(void* pPtr, int, char*, int)
    {
        ms_csLock.Lock();
        ms_ResourcePoolManager.HeapFree(pPtr);
        ms_csLock.Unlock();
    }

    static cResourcePoolManager* GetPoolManager() { return &(ms_ResourcePoolManager); }

    static void Uninitialize()
    {
        ms_ResourcePoolManager.Uninitialize();
    }
};

template <typename T, size_t BlockSize, bool DELETE_OPTION>
cResourcePoolManager cResourcePoolLock<T, BlockSize, DELETE_OPTION>::ms_ResourcePoolManager(sizeof(T), BlockSize, DELETE_OPTION);
template <typename T, size_t BlockSize, bool DELETE_OPTION>
sCSLock cResourcePoolLock<T, BlockSize, DELETE_OPTION>::ms_csLock;

template <typename T, size_t BlockSize>
inline void* operator new(size_t size, cResourcePoolLock<T, BlockSize>& pool)
{
    return pool.GetPoolManager()->HeapAlloc();
}

template <typename T, size_t BlockSize>
inline void Del(T* ptr, cResourcePoolLock<T, BlockSize>& pool)
{
    ptr->~T();
    pool.GetPoolManager()->HeapFree(ptr);
}

// class public 내부와 클래스 외부에 선언해줄것
#define Inner_Decl_ResourcePoolLock(ClassType, Size) \
    static cResourcePoolLock<##ClassType, ##Size> ms_kPool##ClassType; \
public: \
    void operator delete(void* pPtr) \
    { ms_kPool##ClassType.GetPoolManager()->HeapFree(pPtr); } \
    \
    void* operator new(size_t n) \
    { return ms_kPool##ClassType.GetPoolManager()->HeapAlloc(); }

#define Outer_Decl_ResourcePoolLock(ClassType, Size) \
    cResourcePoolLock<##ClassType, ##Size> ##ClassType::ms_kPool##ClassType;


class cEntity
{
public:
    int m_nID;
    int m_nHP;
    int m_nMP;

    Inner_Decl_ResourcePoolUnLock(cEntity, 10);
};

Outer_Decl_ResourcePoolUnLock(cEntity, 10);

int main()
{
    std::cout << "Hello World!\n";

    std::vector<cEntity*> vecEntity;

    for (int n = 0; n < 20; n++)
    {
        cEntity* pEntity = new cEntity;

        pEntity->m_nID = 1;
        pEntity->m_nHP = 100;
        pEntity->m_nMP = 50;

        vecEntity.emplace_back(pEntity);
    }

    for (auto& pEntity : vecEntity)
    {
        delete pEntity;
	}

    int a = 0;
}

// 프로그램 실행: <Ctrl+F5> 또는 [디버그] > [디버깅하지 않고 시작] 메뉴
// 프로그램 디버그: <F5> 키 또는 [디버그] > [디버깅 시작] 메뉴

// 시작을 위한 팁: 
//   1. [솔루션 탐색기] 창을 사용하여 파일을 추가/관리합니다.
//   2. [팀 탐색기] 창을 사용하여 소스 제어에 연결합니다.
//   3. [출력] 창을 사용하여 빌드 출력 및 기타 메시지를 확인합니다.
//   4. [오류 목록] 창을 사용하여 오류를 봅니다.
//   5. [프로젝트] > [새 항목 추가]로 이동하여 새 코드 파일을 만들거나, [프로젝트] > [기존 항목 추가]로 이동하여 기존 코드 파일을 프로젝트에 추가합니다.
//   6. 나중에 이 프로젝트를 다시 열려면 [파일] > [열기] > [프로젝트]로 이동하고 .sln 파일을 선택합니다.
