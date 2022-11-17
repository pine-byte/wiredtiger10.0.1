/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#define WT_TXN_NONE 0                /* Beginning of time */
#define WT_TXN_FIRST 1               /* First transaction to run */
#define WT_TXN_MAX (UINT64_MAX - 10) /* End of time */
#define WT_TXN_ABORTED UINT64_MAX    /* Update rolled back */

#define WT_TS_NONE 0         /* Beginning of time */
#define WT_TS_MAX UINT64_MAX /* End of time */

/* AUTOMATIC FLAG VALUE GENERATION START 0 */
#define WT_TXN_LOG_CKPT_CLEANUP 0x01u
#define WT_TXN_LOG_CKPT_PREPARE 0x02u
#define WT_TXN_LOG_CKPT_START 0x04u
#define WT_TXN_LOG_CKPT_STOP 0x08u
#define WT_TXN_LOG_CKPT_SYNC 0x10u
/* AUTOMATIC FLAG VALUE GENERATION STOP 32 */

/* AUTOMATIC FLAG VALUE GENERATION START 0 */
#define WT_TXN_OLDEST_STRICT 0x1u
#define WT_TXN_OLDEST_WAIT 0x2u
/* AUTOMATIC FLAG VALUE GENERATION STOP 32 */

/* AUTOMATIC FLAG VALUE GENERATION START 0 */
#define WT_TXN_TS_ALREADY_LOCKED 0x1u
#define WT_TXN_TS_INCLUDE_CKPT 0x2u
#define WT_TXN_TS_INCLUDE_OLDEST 0x4u
/* AUTOMATIC FLAG VALUE GENERATION STOP 32 */

typedef enum {
    WT_VISIBLE_FALSE = 0,   /* Not a visible update */
    WT_VISIBLE_PREPARE = 1, /* Prepared update */
    WT_VISIBLE_TRUE = 2     /* A visible update */
} WT_VISIBLE_TYPE;

/*
 * Transaction ID comparison dealing with edge cases.
 *
 * WT_TXN_ABORTED is the largest possible ID (never visible to a running transaction), WT_TXN_NONE
 * is smaller than any possible ID (visible to all running transactions).
 */
#define WT_TXNID_LE(t1, t2) ((t1) <= (t2))

#define WT_TXNID_LT(t1, t2) ((t1) < (t2))

#define WT_SESSION_TXN_SHARED(s)                         \
    (S2C(s)->txn_global.txn_shared_list == NULL ? NULL : \
                                                  &S2C(s)->txn_global.txn_shared_list[(s)->id])

#define WT_SESSION_IS_CHECKPOINT(s) ((s)->id != 0 && (s)->id == S2C(s)->txn_global.checkpoint_id)

/*
 * Perform an operation at the specified isolation level.
 *
 * This is fiddly: we can't cope with operations that begin transactions (leaving an ID allocated),
 * and operations must not move our published snap_min forwards (or updates we need could be freed
 * while this operation is in progress). Check for those cases: the bugs they cause are hard to
 * debug.
 */
#define WT_WITH_TXN_ISOLATION(s, iso, op)                                       \
    do {                                                                        \
        WT_TXN_ISOLATION saved_iso = (s)->isolation;                            \
        WT_TXN_ISOLATION saved_txn_iso = (s)->txn->isolation;                   \
        WT_TXN_SHARED *txn_shared = WT_SESSION_TXN_SHARED(s);                   \
        WT_TXN_SHARED saved_txn_shared = *txn_shared;                           \
        (s)->txn->forced_iso++;                                                 \
        (s)->isolation = (s)->txn->isolation = (iso);                           \
        op;                                                                     \
        (s)->isolation = saved_iso;                                             \
        (s)->txn->isolation = saved_txn_iso;                                    \
        WT_ASSERT((s), (s)->txn->forced_iso > 0);                               \
        (s)->txn->forced_iso--;                                                 \
        WT_ASSERT((s),                                                          \
          txn_shared->id == saved_txn_shared.id &&                              \
            (txn_shared->metadata_pinned == saved_txn_shared.metadata_pinned || \
              saved_txn_shared.metadata_pinned == WT_TXN_NONE) &&               \
            (txn_shared->pinned_id == saved_txn_shared.pinned_id ||             \
              saved_txn_shared.pinned_id == WT_TXN_NONE));                      \
        txn_shared->metadata_pinned = saved_txn_shared.metadata_pinned;         \
        txn_shared->pinned_id = saved_txn_shared.pinned_id;                     \
    } while (0)

struct __wt_txn_shared {
    WT_CACHE_LINE_PAD_BEGIN
    volatile uint64_t id;
    volatile uint64_t pinned_id;
    volatile uint64_t metadata_pinned;

    /*
     * The first commit or durable timestamp used for this transaction. Determines its position in
     * the durable queue and prevents the all_durable timestamp moving past this point.
     ** 用于此事务的第一个提交或持久时间戳。 确定其在持久队列中的位置并防止 all_durable 时间戳移过该点。
     ** 感觉 all_durable 也像是最老，在queue中是最老的
     */
    wt_timestamp_t pinned_durable_timestamp;

    /*
     * The read timestamp used for this transaction. Determines what updates can be read and
     * prevents the oldest timestamp moving past this point.
     ** 用于此事务的读取时间戳[最老还是最新？感觉偏像老]。 确定可以读取哪些更新并防止最旧的时间戳移过此点。
     */
    wt_timestamp_t read_timestamp;

    volatile uint8_t is_allocating;
    WT_CACHE_LINE_PAD_END
};

struct __wt_txn_global {
    volatile uint64_t current; /* Current transaction ID. */

    /* The oldest running transaction ID (may race). 最早运行的事务 ID */
    volatile uint64_t last_running;

    /*
     * The oldest transaction ID that is not yet visible to some transaction in the system.
     * 系统中某些事务尚不可见的最早事务 ID。
     */
    volatile uint64_t oldest_id;

    wt_timestamp_t durable_timestamp;
    wt_timestamp_t last_ckpt_timestamp;
    wt_timestamp_t meta_ckpt_timestamp;
    wt_timestamp_t oldest_timestamp;
    wt_timestamp_t pinned_timestamp; // 不太清楚干啥的
    wt_timestamp_t recovery_timestamp;
    wt_timestamp_t stable_timestamp;
    bool has_durable_timestamp;
    bool has_oldest_timestamp;
    bool has_pinned_timestamp;
    bool has_stable_timestamp;
    bool oldest_is_pinned;
    bool stable_is_pinned;

    /* Protects the active transaction states. 保护活动事务状态。 */
    WT_RWLOCK rwlock;

    /* Protects logging, checkpoints and transaction visibility. 保护日志记录、检查点和事务可见性。 */
    WT_RWLOCK visibility_rwlock;

    /*
     * Track information about the running checkpoint. The transaction snapshot used when
     * checkpointing are special. Checkpoints can run for a long time so we keep them out of regular
     * visibility checks. Eviction and checkpoint operations know when they need to be aware of
     * checkpoint transactions.
     ** 跟踪有关正在运行的检查点的信息。 检查点时使用的事务快照是特殊的。 检查点可以运行很长时间，
     ** 因此我们将它们排除在常规可见性检查之外。 驱逐和检查点操作知道何时需要了解检查点事务。
     *
     * We rely on the fact that (a) the only table a checkpoint updates is the metadata; and (b)
     * once checkpoint has finished reading a table, it won't revisit it.
     ** 我们依赖以下事实： (a) 检查点更新的唯一表是元数据； (b) 一旦检查点读完一个表，它就不会重新访问它。
     */
    volatile bool checkpoint_running;    /* Checkpoint running */
    volatile bool checkpoint_running_hs; /* Checkpoint running and processing history store file */
    // checkpoint 会话id
    volatile uint32_t checkpoint_id;     /* Checkpoint's session ID */
    // checkpoint 共享的事务id，有些像全局变量，此操作没有并发，只能同时只有一个运行，所以是单个
    WT_TXN_SHARED checkpoint_txn_shared; /* Checkpoint's txn shared state */
    wt_timestamp_t checkpoint_timestamp; /* Checkpoint's timestamp */
    // checkpoint 预定的事务id，暂不清楚
    volatile uint64_t checkpoint_reserved_txn_id; /* A transaction ID reserved by checkpoint for
                                            prepared transaction resolution. */

    volatile uint64_t debug_ops;       /* Debug mode op counter */
    uint64_t debug_rollback;           /* Debug mode rollback */
    volatile uint64_t metadata_pinned; /* Oldest ID for metadata */

    // 每个 session 里只有一个 WT_TXN_SHARED， 有多少个 session 就有多少个 WT_TXN_SHARED
    // 多个 session 进行并发抢占，根据session id进行获取对应的下标
    WT_TXN_SHARED *txn_shared_list; /* Per-session shared transaction states */
};

typedef enum __wt_txn_isolation {
    WT_ISO_READ_COMMITTED,
    WT_ISO_READ_UNCOMMITTED,
    WT_ISO_SNAPSHOT
} WT_TXN_ISOLATION;

typedef enum __wt_txn_type {
    WT_TXN_OP_NONE = 0,
    WT_TXN_OP_BASIC_COL,
    WT_TXN_OP_BASIC_ROW,
    WT_TXN_OP_INMEM_COL,
    WT_TXN_OP_INMEM_ROW,
    WT_TXN_OP_REF_DELETE,
    WT_TXN_OP_TRUNCATE_COL,
    WT_TXN_OP_TRUNCATE_ROW
} WT_TXN_TYPE;

typedef enum {
    WT_TXN_TRUNC_ALL,
    WT_TXN_TRUNC_BOTH,
    WT_TXN_TRUNC_START,
    WT_TXN_TRUNC_STOP
} WT_TXN_TRUNC_MODE;

/*
 * WT_TXN_OP -- 事务操作单元
 *	A transactional operation.  Each transaction builds an in-memory array
 *	of these operations as it runs, then uses the array to either write log
 *	records during commit or undo the operations during rollback.
 */
struct __wt_txn_op {
    WT_BTREE *btree;
    WT_TXN_TYPE type;
    union {
        /* WT_TXN_OP_BASIC_ROW, WT_TXN_OP_INMEM_ROW */
        struct {
            WT_UPDATE *upd;
            WT_ITEM key;
        } op_row;

        /* WT_TXN_OP_BASIC_COL, WT_TXN_OP_INMEM_COL */
        struct {
            WT_UPDATE *upd;
            uint64_t recno;
        } op_col;
/*
 * upd is pointing to same memory in both op_row and op_col, so for simplicity just chose op_row upd
 */
#undef op_upd
#define op_upd op_row.upd

        /* WT_TXN_OP_REF_DELETE */
        WT_REF *ref;
        /* WT_TXN_OP_TRUNCATE_COL */
        struct {
            uint64_t start, stop;
        } truncate_col;
        /* WT_TXN_OP_TRUNCATE_ROW */
        struct {
            WT_ITEM start, stop;
            WT_TXN_TRUNC_MODE mode;
        } truncate_row;
    } u;

/* AUTOMATIC FLAG VALUE GENERATION START 0 */
#define WT_TXN_OP_KEY_REPEATED 0x1u
    /* AUTOMATIC FLAG VALUE GENERATION STOP 32 */
    uint32_t flags;
};

/*
 * WT_TXN --
 *	Per-session transaction context.
 ** txn核心结构体
 */
struct __wt_txn {
    uint64_t id;
    // 枚举类，隔离级别
    WT_TXN_ISOLATION isolation;

    uint32_t forced_iso; /* Isolation is currently forced. 说白了就是写死了，只支持版本隔离级别*/

    /*
     * Snapshot data: 
     ** 版本数据规则
     *	ids >= snap_max are invisible,
     ** 永远不可见【包含最大】
     *	ids < snap_min are visible,
     ** 可见【不包含】
     *	everything else is visible unless it is in the snapshot.
     */
    uint64_t snap_min, snap_max;
    // 版本id数组以及大小
    uint64_t *snapshot;
    uint32_t snapshot_count;
    uint32_t txn_logsync; /* Log sync configuration */

    /*
     * Timestamp copied into updates created by this transaction.
     ** 初始化updates中的时间戳, 关于时间戳的东西我还没太搞明白
     * In some use cases, this can be updated while the transaction is running.
     ** 这些东西还是可能被更改的
     */
    wt_timestamp_t commit_timestamp;

    /*
     * Durable timestamp copied into updates created by this transaction. It is used to decide
     * whether to consider this update to be persisted or not by stable checkpoint.
     ** 初始化持久时间戳。 它用于决定是否考虑将此更新通过稳定检查点持久化。
     */
    wt_timestamp_t durable_timestamp;

    /*
     * Set to the first commit timestamp used in the transaction and fixed while the transaction is
     * on the public list of committed timestamps.
     ** 设置为事务中使用的第一个提交时间戳，并在事务位于提交时间戳的公共列表时固定。
     ** 首先明白这里有一个 提交时间戳的公共列表，我感觉来判断提交的先后顺序来判断冲突的成功先后抢占
     */
    wt_timestamp_t first_commit_timestamp;

    /*
     * Timestamp copied into updates created by this transaction, when this transaction is prepared.
     ** 初始化 prepared 时间戳
     */
    wt_timestamp_t prepare_timestamp;

    /* Array of modifications by this transaction. */
    // 这个事务中的实际操作数组
    WT_TXN_OP *mod;
    size_t mod_alloc;
    u_int mod_count;
#ifdef HAVE_DIAGNOSTIC
    u_int prepare_count;
#endif

    /* Scratch buffer for in-memory log records. */
    // 日志, 内存管理其实也挺复杂的，可以先忽略, 应该也是个数组，对应mod数组
    WT_ITEM *logrec;

    /* Requested notification when transactions are resolved. */
    // 事务完成后的一些信号通知，具体不知道干啥用的, 暂不猜测
    WT_TXN_NOTIFY *notify;

    /* Checkpoint status. 检查点的一些状态 */
    WT_LSN ckpt_lsn;
    uint32_t ckpt_nsnapshot;
    WT_ITEM *ckpt_snapshot;
    bool full_ckpt;

    /* Timeout */
    uint64_t operation_timeout_us;

    const char *rollback_reason; /* If rollback, the reason 记录事务回滚的一些原因 */

/*
 * WT_TXN_HAS_TS_COMMIT --
 *	The transaction has a set commit timestamp.
 **     是否提交flag
 * WT_TXN_HAS_TS_DURABLE --
 *	The transaction has an explicitly set durable timestamp (that is, it
 *	hasn't been mirrored from its commit timestamp value).
 **     是否持久化flag
 * WT_TXN_SHARED_TS_DURABLE --
 *	The transaction has been published to the durable queue. Setting this
 *	flag lets us know that, on release, we need to mark the transaction for
 *	clearing.
 **     durable queue 持久化有个队列，不太清楚
 */

/* AUTOMATIC FLAG VALUE GENERATION START 0 */
#define WT_TXN_AUTOCOMMIT 0x0000001u
#define WT_TXN_ERROR 0x0000002u
#define WT_TXN_HAS_ID 0x0000004u
#define WT_TXN_HAS_SNAPSHOT 0x0000008u
#define WT_TXN_HAS_TS_COMMIT 0x0000010u
#define WT_TXN_HAS_TS_DURABLE 0x0000020u
#define WT_TXN_HAS_TS_PREPARE 0x0000040u
#define WT_TXN_IGNORE_PREPARE 0x0000080u
#define WT_TXN_PREPARE 0x0000100u
#define WT_TXN_PREPARE_IGNORE_API_CHECK 0x0000200u
#define WT_TXN_READONLY 0x0000400u
#define WT_TXN_RUNNING 0x0000800u
#define WT_TXN_SHARED_TS_DURABLE 0x0001000u
#define WT_TXN_SHARED_TS_READ 0x0002000u
#define WT_TXN_SYNC_SET 0x0004000u
#define WT_TXN_TS_READ_BEFORE_OLDEST 0x0008000u
#define WT_TXN_TS_ROUND_PREPARED 0x0010000u
#define WT_TXN_TS_ROUND_READ 0x0020000u
#define WT_TXN_TS_WRITE_ALWAYS 0x0040000u
#define WT_TXN_TS_WRITE_KEY_CONSISTENT 0x0080000u
#define WT_TXN_TS_WRITE_MIXED_MODE 0x0100000u
#define WT_TXN_TS_WRITE_NEVER 0x0200000u
#define WT_TXN_TS_WRITE_ORDERED 0x0400000u
#define WT_TXN_UPDATE 0x0800000u
#define WT_TXN_VERB_TS_WRITE 0x1000000u
    /* AUTOMATIC FLAG VALUE GENERATION STOP 32 */
    uint32_t flags;

    /*
     * Zero or more bytes of value (the payload) immediately follows the WT_UPDATE structure. We use
     * a C99 flexible array member which has the semantics we want.
     */
    uint64_t __snapshot[];
};
