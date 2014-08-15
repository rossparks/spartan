#ifndef CCORE_H
#define CCORE_H

#include <string>
#include <vector>
#include <map>

#include "array/ctile.h"

/**
 * A `TileId` uniquely identifies a tile in a Spartan execution.
 *
 * Currently, TileId instances consist of a worker index and a blob
 * index for that worker.
 */
struct TileId {
    int32_t worker, id;

    TileId(int32_t w, int32_t i): worker(w), id(i) {}

    TileId(): id(-1) {}

    bool operator==(const TileId & other) const {
        return worker == other.worker && id == other.id;
    }

    std::string to_string() const {
        return "B(" + std::to_string(worker) + "." + std::to_string(id) + ")";
    }
};

namespace std {
    template <>
    struct hash<TileId> {
        std::size_t operator()(const TileId& k) const {
            return (size_t)(k.worker ^ k.id);
        }
    };
}

inline rpc::Marshal& operator <<(rpc::Marshal& m, const TileId& o) {
    m << o.worker;
    m << o.id;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, TileId& o) {
    m >> o.worker;
    m >> o.id;
    return m;
}

/**
 * Status information sent to the master in a heartbeat message.
 */
struct WorkerStatus {
    int64_t total_physical_memory;
    int32_t num_processors;
    double mem_usage, cpu_usage, last_report_time;
    std::vector<TileId>& kernel_remain_tiles;
    std::vector<TileId> new_tiles;

    WorkerStatus(int64_t phy_memory, int32_t proc_num,
                 float m_usage, float c_usage, double report_time,
                 std::vector<TileId>& remain_tiles):total_physical_memory(phy_memory),
                                                    num_processors(proc_num),
                                                    mem_usage(m_usage),
                                                    cpu_usage(c_usage),
                                                    last_report_time(report_time),
                                                    kernel_remain_tiles(remain_tiles) {}

    WorkerStatus(): kernel_remain_tiles(new_tiles) {}

    void update_status(float m_usage, float c_usage, double report_time) {
        mem_usage = m_usage;
        cpu_usage = c_usage;
        last_report_time = report_time;
    }

    void clean_status() {
        kernel_remain_tiles.clear();
    }

    std::string to_string() const {
        std::string str =  "WS:total_phy_mem:" + std::to_string(total_physical_memory) +
            " num_processors:" + std::to_string(num_processors) +
            " mem_usage:" + std::to_string(mem_usage) + " cpu_usage:" + std::to_string(cpu_usage) +
            " remain_tiles:[";
        for (std::vector<TileId>::iterator it = kernel_remain_tiles.begin() ; it != kernel_remain_tiles.end(); ++it) {
            str += (*it).to_string() + ",";
        }
        return str + "]";
    }
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const WorkerStatus& o) {
    m << o.total_physical_memory;
    m << o.num_processors;
    m << o.mem_usage;
    m << o.cpu_usage;
    m << o.last_report_time;
    m << o.kernel_remain_tiles;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, WorkerStatus& o) {
    m >> o.total_physical_memory;
    m >> o.num_processors;
    m >> o.mem_usage;
    m >> o.cpu_usage;
    m >> o.last_report_time;
    m >> o.kernel_remain_tiles;
    return m;
}

/**
 * C++ version of slice.
 * It is the same as the python slice which contains the start, stop and step
 * information.
 */
struct Slice {
    int64_t start;
    int64_t stop;
    int64_t step;

    Slice(int64_t sta, int64_t sto, int64_t ste): start(sta), stop(sto), step(ste) {}
    Slice() {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const Slice& o) {
    m << o.start;
    m << o.stop;
    m << o.step;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, Slice& o) {
    m >> o.start;
    m >> o.stop;
    m >> o.step;
    return m;
}

/**
 * C++ version of SubSlice.
 * It is a list of slices each of which represents a dim.
 */
struct SubSlice {
    std::vector<Slice> slices;
    SubSlice() {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const SubSlice& o) {
    m << o.slices;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, SubSlice& o) {
    m >> o.slices;
    return m;
}


struct EmptyMessage {
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const EmptyMessage& o) {
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, EmptyMessage& o) {
    return m;
}

/**
 * Sent by worker to master when registering during startup.
 */
struct RegisterReq {
    std::string host;
    WorkerStatus worker_status;
    RegisterReq(const std::string h, const WorkerStatus& ws): host(h), worker_status(ws) {}
    RegisterReq() {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const RegisterReq& o) {
    m << o.host;
    m << o.worker_status;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, RegisterReq& o) {
    m >> o.host;
    m >> o.worker_status;
    return m;
}

/**
 * Sent from the master to a worker after all workers have registered.
 * Contains the workers unique identifier and a list of all other workers in the execution.
 */
struct InitializeReq {
    int32_t id;
    std::unordered_map<int32_t, std::string> peers;
    InitializeReq(int32_t i, const std::unordered_map<int32_t, std::string>& p): id(i), peers(p) {}
    InitializeReq() {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const InitializeReq& o) {
    m << o.id;
    m << o.peers;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, InitializeReq& o) {
    m >> o.id;
    m >> o.peers;
    return m;
}

/**
 * Fetch a region from a tile.
 */
struct GetReq {
    TileId id;
    SubSlice subslice;
    GetReq(const TileId& i, const SubSlice& s): id(i), subslice(s) {}
    GetReq() {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const GetReq& o) {
    m << o.id;
    m << o.subslice;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, GetReq& o) {
    m >> o.id;
    m >> o.subslice;
    return m;
}

/**
 * The result of a fetch operation: the tile fetched from and the resulting data.
 */
struct GetResp {
    TileId id;
    std::string& data;
    std::string new_data;

    GetResp(const TileId& i, std::string& d): id(i), data(d) {}
    GetResp(std::string& d): data(d) {}
    GetResp(): data(new_data) {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const GetResp& o) {
    m << o.id;
    m << o.data;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, GetResp& o) {
    m >> o.id;
    m >> o.data;
    return m;
}

/**
 * Destroy any tiles listed in ``ids``.
 */
struct DestroyReq {
    std::vector<TileId>& ids;
    std::vector<TileId> new_ids;
    DestroyReq(std::vector<TileId>& i): ids(i) {}
    DestroyReq(): ids(new_ids) {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const DestroyReq& o) {
    m << o.ids;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, DestroyReq& o) {
    m >> o.ids;
    return m;
}

/**
 * Update ``region`` (a slice, or None) of tile with id ``id`` .
 *
 * ``data`` should be a Numpy or sparse array.  ``data`` is combined with
 *  existing tile data using the supplied reducer function.
 */
struct UpdateReq {
    TileId id;
    SubSlice region;
    std::string& data;
    std::string new_data;
    int32_t reducer;

    UpdateReq(const TileId& i, const SubSlice& r, std::string& d, int32_t red): id(i), region(r), data(d), reducer(red) {}
    UpdateReq(): data(new_data) {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const UpdateReq& o) {
    m << o.id;
    m << o.region;
    m << o.data;
    m << o.reducer;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, UpdateReq& o) {
    m >> o.id;
    m << o.region;
    m << o.data;
    m << o.reducer;
    return m;
}

/**
 * Run ``fn`` on the list of tiles ``tiles``.
 * For efficiency, the same message is sent to all workers.
 */
struct RunKernelReq {
    std::vector<TileId>& blobs;
    std::vector<TileId> new_blobs;
    std::string& fn;
    std::string new_fn;

    RunKernelReq(std::vector<TileId>& b, std::string& f): blobs(b), fn(f) {}
    RunKernelReq(): blobs(new_blobs), fn(new_fn) {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const RunKernelReq& o) {
    m << o.blobs;
    m << o.fn;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, RunKernelReq& o) {
    m >> o.blobs;
    m >> o.fn;
    return m;
}

/**
 * The result returned from running a kernel function.
 * This is typically a map from `Extent` to TileId.
 */
struct RunKernelResp {
    std::string& result;
    std::string new_result;

    RunKernelResp(std::string& r): result(r) {}
    RunKernelResp(): result(new_result) {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const RunKernelResp& o) {
    m << o.result;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, RunKernelResp& o) {
    m >> o.result;
    return m;
}

/**
 * Create a new tile in a worker.
 * Contain Tile information and TileId.
 */
struct CreateTileReq {
    TileId tile_id;
    CTile data;
    CreateTileReq(const TileId& tid, const CTile& d): tile_id(tid), data(d) {}
    CreateTileReq() {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const CreateTileReq& o) {
    m << o.tile_id;
    m << o.data;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, CreateTileReq& o) {
    m >> o.tile_id;
    m >> o.data;
    return m;
}

/**
 * Send or Receive TileId information.
 */
struct TileIdMessage {
    TileId tile_id;

    TileIdMessage(const TileId& tid): tile_id(tid) {}
    TileIdMessage() {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const TileIdMessage& o) {
    m << o.tile_id;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, TileIdMessage& o) {
    m >> o.tile_id;
    return m;
}

/**
 * The heartbeat message send from worker to master.
 * It contains worker_id and its worker_status information.
 */
struct HeartbeatReq {
    int32_t worker_id;
    WorkerStatus worker_status;

    HeartbeatReq(int32_t wid, const WorkerStatus& ws): worker_id(wid), worker_status(ws) {}
    HeartbeatReq() {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const HeartbeatReq& o) {
    m << o.worker_id;
    m << o.worker_status;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, HeartbeatReq& o) {
    m >> o.worker_id;
    m >> o.worker_status;
    return m;
}

/**
 * Steal Tile Request.
 * It also contains the old and new version TileId of previous stealed tile
 * which need to be updated in the master side.
 */
struct UpdateAndStealTileReq {
    int32_t worker_id;
    TileId old_tile_id;
    TileId new_tile_id;

    UpdateAndStealTileReq(int32_t wid, const TileId& oid, const TileId& nid): worker_id(wid), old_tile_id(oid), new_tile_id(nid) {}
    UpdateAndStealTileReq() {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const UpdateAndStealTileReq& o) {
    m << o.worker_id;
    m << o.old_tile_id;
    m << o.new_tile_id;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, UpdateAndStealTileReq& o) {
    m >> o.worker_id;
    m >> o.old_tile_id;
    m >> o.new_tile_id;
    return m;
}

/**
 * Get the tile information from the worker.
 * It contains the element dtype and the sparse information.
 */
struct TileInfoResp {
    std::string dtype;
    int8_t sparse;
    TileInfoResp() {}
};

inline rpc::Marshal& operator <<(rpc::Marshal& m, const TileInfoResp& o) {
    m << o.dtype;
    m << o.sparse;
    return m;
}

inline rpc::Marshal& operator >>(rpc::Marshal& m, TileInfoResp& o) {
    m >> o.dtype;
    m >> o.sparse;
    return m;
}

#endif