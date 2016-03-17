/*!
 * \file      c_thread_pool.cpp
 * \brief
 *    定义线程池的实现
 * Copyright (c) 2003，2004 Asiainfo Technologies(China),Inc.
 * RCS: $Id: c_thread_pool.h,v 1.1 2006/09/18 08:17:07 yangxw Exp $
 *
 * History
 *  2004/06/21 Yangxiuwu first release
 *  2005/10/20 yxw CWorkerList中增加erase_worker和clear函数
 *  2006/05/29 yxw CWorkThread中增加is_stop函数用于判断线程是和启动完成
 *  2006/07/10 yxw 为避免内存无限制的增长，增加对队列大小的限制
 */
//---------------------------------------------------------------------------

#ifndef c_thread_poolH
#define c_thread_poolH
//---------------------------------------------------------------------------
#include <algorithm>

#include "c_thread.h"
#include "c_mutex.h"
#include "c_event.h"
#include "c_task_queue.h"


class CThreadPool;
//------------------------------------------------------------------------
// 类 　: CWorkThread
// 用途 : 工作线程
//------------------------------------------------------------------------
class CWorkThread : public CRunable
{
private:
    CThread *     m_pThread;
    CEvent        m_cEvent;
    CThreadPool * m_pThreadPool;
    CTask *       m_pTask;
    bool          m_bStop;

    int           m_nTotalThreads;
    int           m_nBusiThreads;
public:
    explicit CWorkThread(CThreadPool *pThreadPool);
    ~CWorkThread();
    CThread * get_thread() const
    {
        return m_pThread;
    }
    void set_task(CTask *pTask);
    void run();
    void stop();
    bool is_stop(){return m_bStop;}
};

//------------------------------------------------------------------------
// 类 　: CQueueHandler
// 用途 : 处理任务队列，将任务分配给空闲的工作线程
//------------------------------------------------------------------------
class CQueueHandler : public CRunable
{
private:
    CThread *     m_pThread;
    CThreadPool * m_pThreadPool;
    bool          m_bStop;
public:
    explicit CQueueHandler(CThreadPool *pThreadPool);
    ~CQueueHandler();
    void run();
    void stop();
};

//------------------------------------------------------------------------
// 类 　: CHarvester
// 用途 : 空闲线程的回收
//------------------------------------------------------------------------
class CHarvester : public CRunable
{
private:
    CThread *   m_pThread;
    CThreadPool *m_pThreadPool;
    bool        m_bStop;
public:
    explicit CHarvester(CThreadPool *pThreadPool);
    ~CHarvester();
    void run();
    void stop();
};

const int OBSS_MAXTHREADS = 200;
const int OBSS_MAXIDLE = 10;
const int OBSS_MINIDLE = 5;
const int OBSS_QUEUETIMEOUT = 60;
const int OBSS_HARVESTINTERVAL = 60;
const int OBSS_QUEUEMAXSIZE = 100;


//------------------------------------------------------------------------
// 类 　: CWorkerList
// 用途 : 对工作线程实现列表维护
//------------------------------------------------------------------------
class CWorkerList
{
private:

    AISTD vector<CWorkThread *> m_lstWorker;
    CEvent m_cEvent;
    CMutex m_cMutex;
public:
    CWorkThread * pop_worker()
    {
        CWorkThread * pWorker = 0;
        if (!m_lstWorker.empty())
        {
            pWorker = m_lstWorker.front();
            m_lstWorker.erase(m_lstWorker.begin());
        }
        else
            pWorker = 0;
        return pWorker;
    }

    void push_worker(CWorkThread *pWorker)
    {
        m_lstWorker.push_back(pWorker);
    }

    void erase_worker(CWorkThread *pWorker)
    {
        AISTD vector<CWorkThread *>::iterator it;

        it = AISTD find(m_lstWorker.begin(), m_lstWorker.end(), pWorker);
        if (it != m_lstWorker.end())
            m_lstWorker.erase(it);
    }

    void clear()
    {
    	m_lstWorker.clear();
    }

    void wait()
    {
        m_cEvent.wait();
    }

    int wait(int nSec)
    {
        return m_cEvent.wait(nSec);
    }

    void notify()
    {
        m_cEvent.notify();
    }
};

//------------------------------------------------------------------------
// 类 　: CThreadPool
// 用途 : 线程池的实现封装
//------------------------------------------------------------------------
class CThreadPool
{
private:
    int m_nMaxThreads;
    int m_nMinIdleThreads;
    int m_nMaxIdleThreads;

    int m_nQueueTimeOut;
    int m_nQueueMaxSize; //2006.07.10增加对队列大小的限制
    int m_nHarvestInterval;

    int m_nTotalThreads;
    int m_nBusiThreads;

    bool m_bStop;
    bool m_bNeedNotifyIdle; //2006.05.30增加用于判断线程返回到线程池时
                            //是否需要通知

    bool m_bTaskQueueFull;  //2006.07.10增加判断队列是否已经达到最大值
    CMutex m_cQueueSizeMutex;//2006.07.10增加用于处理队列满的情况
    CEvent m_cQueueSizeEvent;//2006.07.10增加用于处理队列满的情况

    CMutex m_cMutex;

    CTaskQueue m_cTaskQueue;
    CWorkerList m_cIdleList;
    CWorkerList m_cWorkerList;

    CQueueHandler *m_pQueueHandler;
    CHarvester  *m_pHarvester;

public:
    int get_harvestInterval() const
    {
        return m_nHarvestInterval;
    }

    int get_busiThreadCount() const
    {
        return m_nBusiThreads;
    }

    int get_totalThreadCount() const
    {
        return m_nTotalThreads;
    }

public:
    explicit CThreadPool(int nMaxThreads = OBSS_MAXTHREADS,
                         int nMinIdleThreads = OBSS_MINIDLE,
                         int nMaxIdleThreads = OBSS_MAXIDLE,
                         int nQueueTimeOut = OBSS_QUEUETIMEOUT,
                         int nQueueMaxSize = OBSS_QUEUEMAXSIZE,    //增加队列大小限制
                         int nHarvestInterval = OBSS_HARVESTINTERVAL);
    ~CThreadPool();

    void ret_toPool(CWorkThread *pWork);
    void run_task(PTask pTask);

    void push_task(PTask pTask)
    {
        /*
        ** 2006.07.10 增加对任务队列满时的处理
        */
        if (m_cTaskQueue.size() >= m_nQueueMaxSize)
        {
            CAutoMutex mutex(&m_cQueueSizeMutex);
            mutex.lock();
            m_bTaskQueueFull = true;
            mutex.unlock();

            int nRet = m_cQueueSizeEvent.wait(20);
            m_cQueueSizeEvent.reset();//消除多余的notify

            if (m_bStop)
            {
                pTask->timeout();
                return;
            }
        }
        /*
        ** 2006.07.10修改结束
        */
        m_cTaskQueue.push_task(pTask);
    }

    CTask * pop_task()
    {
        /*
        ** 2006.07.10 增加对任务队列满时的处理
        */
        CTask *pTask = m_cTaskQueue.pop_task();
        //队列为空时也notify一次，避免队列处理线程与增加任务主线程互相等待
        if ( m_bTaskQueueFull || m_cTaskQueue.size() == 0)
        {
            CAutoMutex mutex(&m_cQueueSizeMutex);
            mutex.lock();
            m_bTaskQueueFull = false;
            mutex.unlock();
            m_cQueueSizeEvent.notify();

        }
        /*
        ** 2006.07.10修改结束
        */

        return pTask;
    }

    void wait_task()
    {
        m_cTaskQueue.wait_task();
    }

    void notify_task()
    {
        m_cTaskQueue.notify_task();
    }

    int task_size()
    {
        return  m_cTaskQueue.size();
    }

    void harvest_spare();
    void create_worker(int nWorkers);
    void start();
    void stop();
};
#endif


