#ifndef CWORKER_H
#define CWORKER_H

#include <assert.h>
#include <ctime>
#include <unordered_map>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <Python.h>

#include "array/ctile.h"
#include "base/threading.h"
#include "base/logging.h"
#include "fastrpc/service.h"

class CBlobCtx;

class CWorker: public spartan::WorkerService {
private:
    int32_t id;
    std::string _addr;
    bool _initialized, _running;

    spartan::MasterProxy* _master; // rpc client to master
    std::unordered_map<int32_t, spartan::WorkerProxy*> _peers; // rpc client to other workers
    CBlobCtx* _ctx; // simple rpc interface
    std::unordered_map<TileId, CTile> _blobs; // all the tiles in this worker
    int32_t _id_counter; // TileId counter in local worker
    std::vector<TileId> _kernel_remain_tiles; // current remain tiles waiting for kernel processing
    WorkerStatus* _worker_status; // worker status of this worker which will be send to master in each heartbeat
    base::SpinLock _blob_lock; // lock for _blobs
    base::SpinLock _kernel_lock; // lock for _kernel_remain_tiles

    rpc::PollMgr* _clt_poll;
    rpc::ClientPool* _clt_pool;

    int32_t HEARTBEAT_INTERVAL = 3;

    inline void lock(base::SpinLock& l) {
        l.lock();
    }

    inline void unlock(base::SpinLock& l) {
        l.unlock();
    }

    inline void shutdown() {
        Log_info("Closing server %d ...", id);
        _running = false;
    }

public:
    CWorker(const std::string& master_addr, const std::string& worker_addr, int32_t heartbeat_interval);
    ~CWorker();
    void register_to_master();
    void wait_for_shutdown();

    // all the rpc services
    void initialize(const InitializeReq& req, EmptyMessage* resp);
    void get_tile_info(const TileIdMessage& req, TileInfoResp* resp);
    void create(const CreateTileReq& req, TileIdMessage* resp);
    void destroy(const DestroyReq& req, EmptyMessage* resp);
    void update(const UpdateReq& req, EmptyMessage* resp);
    void get(const GetReq& req, GetResp* resp);
    void get_flatten(const GetReq& req, GetResp* resp);
    void cancel_tile(const TileIdMessage& req, rpc::i8* resp);
    void run_kernel(const RunKernelReq& req, RunKernelResp* resp);
    void shutdown(const EmptyMessage& req, EmptyMessage* resp) { shutdown(); }
};

#endif
