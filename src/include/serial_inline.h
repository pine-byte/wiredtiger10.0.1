/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * __insert_simple_func --
 *     Worker function to add a WT_INSERT entry to the middle of a skiplist.
 */
static inline int
__insert_simple_func(
  WT_SESSION_IMPL *session, WT_INSERT ***ins_stack, WT_INSERT *new_ins, u_int skipdepth)
{
    u_int i;

    WT_UNUSED(session);

    /*
     * Update the skiplist elements referencing the new WT_INSERT item. If we fail connecting one of
     * the upper levels in the skiplist, return success: the levels we updated are correct and
     * sufficient. Even though we don't get the benefit of the memory we allocated, we can't roll
     * back.
     *
     * All structure setup must be flushed before the structure is entered into the list. We need a
     * write barrier here, our callers depend on it. Don't pass complex arguments to the macro, some
     * implementations read the old value multiple times.
     */
    for (i = 0; i < skipdepth; i++) {
        WT_INSERT *old_ins = *ins_stack[i];
        if (old_ins != new_ins->next[i] || !__wt_atomic_cas_ptr(ins_stack[i], old_ins, new_ins))
            return (i == 0 ? WT_RESTART : 0);
    }

    return (0);
}

/*
 * __insert_serial_func --
 *     Worker function to add a WT_INSERT entry to a skiplist.
 */
static inline int
__insert_serial_func(WT_SESSION_IMPL *session, WT_INSERT_HEAD *ins_head, WT_INSERT ***ins_stack,
  WT_INSERT *new_ins, u_int skipdepth)
{
    u_int i;

    /* The cursor should be positioned. */
    WT_ASSERT(session, ins_stack[0] != NULL);

    /*
     * Update the skiplist elements referencing the new WT_INSERT item.
     *
     * Confirm we are still in the expected position, and no item has been added where our insert
     * belongs. If we fail connecting one of the upper levels in the skiplist, return success: the
     * levels we updated are correct and sufficient. Even though we don't get the benefit of the
     * memory we allocated, we can't roll back.
     *
     * All structure setup must be flushed before the structure is entered into the list. We need a
     * write barrier here, our callers depend on it. Don't pass complex arguments to the macro, some
     * implementations read the old value multiple times.
     */
    for (i = 0; i < skipdepth; i++) {
        WT_INSERT *old_ins = *ins_stack[i];
        if (old_ins != new_ins->next[i] || !__wt_atomic_cas_ptr(ins_stack[i], old_ins, new_ins))
            return (i == 0 ? WT_RESTART : 0);
        if (ins_head->tail[i] == NULL || ins_stack[i] == &ins_head->tail[i]->next[i])
            ins_head->tail[i] = new_ins;
    }

    return (0);
}

/*
 * __col_append_serial_func --
 *     Worker function to allocate a record number as necessary, then add a WT_INSERT entry to a
 *     skiplist.
 */
static inline int
__col_append_serial_func(WT_SESSION_IMPL *session, WT_INSERT_HEAD *ins_head, WT_INSERT ***ins_stack,
  WT_INSERT *new_ins, uint64_t *recnop, u_int skipdepth)
{
    WT_BTREE *btree;
    uint64_t recno;
    u_int i;

    btree = S2BT(session);

    /*
     * If the application didn't specify a record number, allocate a new one and set up for an
     * append.
     */
    if ((recno = WT_INSERT_RECNO(new_ins)) == WT_RECNO_OOB) {
        recno = WT_INSERT_RECNO(new_ins) = btree->last_recno + 1;
        WT_ASSERT(session,
          WT_SKIP_LAST(ins_head) == NULL || recno > WT_INSERT_RECNO(WT_SKIP_LAST(ins_head)));
        for (i = 0; i < skipdepth; i++)
            ins_stack[i] =
              ins_head->tail[i] == NULL ? &ins_head->head[i] : &ins_head->tail[i]->next[i];
    }

    /* Confirm position and insert the new WT_INSERT item. */
    WT_RET(__insert_serial_func(session, ins_head, ins_stack, new_ins, skipdepth));

    /*
     * Set the calling cursor's record number. If we extended the file, update the last record
     * number.
     */
    *recnop = recno;
    if (recno > btree->last_recno)
        btree->last_recno = recno;

    return (0);
}

/*
 * __wt_col_append_serial --
 *     Append a new column-store entry.
 */
static inline int
__wt_col_append_serial(WT_SESSION_IMPL *session, WT_PAGE *page, WT_INSERT_HEAD *ins_head,
  WT_INSERT ***ins_stack, WT_INSERT **new_insp, size_t new_ins_size, uint64_t *recnop,
  u_int skipdepth, bool exclusive)
{
    WT_DECL_RET;
    WT_INSERT *new_ins;

    /* Clear references to memory we now own and must free on error. */
    new_ins = *new_insp;
    *new_insp = NULL;

    /*
     * Acquire the page's spinlock unless we already have exclusive access. Then call the worker
     * function.
     */
    if (!exclusive)
        WT_PAGE_LOCK(session, page);
    ret = __col_append_serial_func(session, ins_head, ins_stack, new_ins, recnop, skipdepth);
    if (!exclusive)
        WT_PAGE_UNLOCK(session, page);

    if (ret != 0) {
        /* Free unused memory on error. */
        __wt_free(session, new_ins);
        return (ret);
    }

    /*
     * Increment in-memory footprint after releasing the mutex: that's safe because the structures
     * we added cannot be discarded while visible to any running transaction, and we're a running
     * transaction, which means there can be no corresponding delete until we complete.
     */
    __wt_cache_page_inmem_incr(session, page, new_ins_size);

    /* Mark the page dirty after updating the footprint. */
    __wt_page_modify_set(session, page);

    return (0);
}

/*
 * __wt_insert_serial --
 *     Insert a row or column-store entry.
 */
static inline int
__wt_insert_serial(WT_SESSION_IMPL *session, WT_PAGE *page, WT_INSERT_HEAD *ins_head,
  WT_INSERT ***ins_stack, WT_INSERT **new_insp, size_t new_ins_size, u_int skipdepth,
  bool exclusive)
{
    WT_DECL_RET;
    WT_INSERT *new_ins;
    u_int i;
    bool simple;

    /* Clear references to memory we now own and must free on error. */
    new_ins = *new_insp;
    *new_insp = NULL;

    simple = true;
    for (i = 0; i < skipdepth; i++)
        if (new_ins->next[i] == NULL)
            simple = false;

    if (simple)
        ret = __insert_simple_func(session, ins_stack, new_ins, skipdepth);
    else {
        if (!exclusive)
            WT_PAGE_LOCK(session, page);
        ret = __insert_serial_func(session, ins_head, ins_stack, new_ins, skipdepth);
        if (!exclusive)
            WT_PAGE_UNLOCK(session, page);
    }

    if (ret != 0) {
        /* Free unused memory on error. */
        __wt_free(session, new_ins);
        return (ret);
    }

    /*
     * Increment in-memory footprint after releasing the mutex: that's safe because the structures
     * we added cannot be discarded while visible to any running transaction, and we're a running
     * transaction, which means there can be no corresponding delete until we complete.
     */
    __wt_cache_page_inmem_incr(session, page, new_ins_size);

    /* Mark the page dirty after updating the footprint. */
    __wt_page_modify_set(session, page);

    return (0);
}

/*
 * __wt_update_serial --
 *     Update a row or column-store entry.
 *     应该是串行化的，好像是mvcc连表添加头结点
 *     srch_upd 应该是之前找到的对应 kv结构体
 *     updp 应该是新value对应的结构体
 */
static inline int
__wt_update_serial(WT_SESSION_IMPL *session, WT_CURSOR_BTREE *cbt, WT_PAGE *page,
  WT_UPDATE **srch_upd, WT_UPDATE **updp, size_t upd_size, bool exclusive)
{
    WT_DECL_RET;
    WT_UPDATE *obsolete, *upd;
    wt_timestamp_t obsolete_timestamp, prev_upd_ts;
    uint64_t txn;

    /* Clear references to memory we now own and must free on error. */
    upd = *updp;
    *updp = NULL;
    prev_upd_ts = WT_TS_NONE;

#ifdef HAVE_DIAGNOSTIC
    prev_upd_ts = upd->prev_durable_ts;
#endif

    /*
     * All structure setup must be flushed before the structure is entered into the list. We need a
     * write barrier here, our callers depend on it.
     ** 在将结构输入列表之前，必须刷新所有结构设置。 我们在这里需要一个写屏障，我们的调用者依赖它。
     ** 我理解就是upd结构体里的数据必须是最新的
     * Swap the update into place. If that fails, a new update was added after our search, we raced.
     * Check if our update is still permitted.
     ** 交换更新到位。 如果失败了，我们会在搜索后添加新的更新，我们就开始比赛了。 检查我们的更新是否仍然被允许。
     */
    // 自旋原子更换连表头指针, 指向当前的头结点
    while (!__wt_atomic_cas_ptr(srch_upd, upd->next, upd)) {
        // cas已经失败，检测一下能不能正常修改，涉及到回滚，【想看回滚细节请看里面】
        if ((ret = __wt_txn_modify_check(session, cbt, upd->next = *srch_upd, &prev_upd_ts)) != 0) {
            /* Free unused memory on error. */
            // 如果失败了释放 当前此upd 内存. 退出，个人理解这种情况其实已经不用回滚了
            __wt_free(session, upd);
            return (ret);
        }
    }
#ifdef HAVE_DIAGNOSTIC
    upd->prev_durable_ts = prev_upd_ts;
#endif

    /*
     * Increment in-memory footprint after swapping the update into place. Safe because the
     * structures we added cannot be discarded while visible to any running transaction, and we're a
     * running transaction, which means there can be no corresponding delete until we complete.
     ** 将更新交换到位后增加内存占用量。 安全是因为我们添加的结构在对任何正在运行的事务可见时不能被丢弃，而且我们是一个正在运行的事务，
     ** 这意味着在我们完成之前不能进行相应的删除。
     */
    __wt_cache_page_inmem_incr(session, page, upd_size);

    /* Mark the page dirty after updating the footprint. */
    // 标记此页已经污染
    __wt_page_modify_set(session, page);

    /*
     * Don't remove obsolete updates in the history store, due to having different visibility rules
     * compared to normal tables. This visibility rule allows different readers to concurrently read
     * globally visible updates, and insert new globally visible updates, due to the reuse of
     * original transaction informations.
     ** 不要删除历史存储中的过时更新，因为与普通表相比具有不同的可见性规则。 由于原始事务信息的重用，
     ** 该可见性规则允许不同的读取器同时读取全局可见更新，并插入新的全局可见更新。
     */
    if (WT_IS_HS(session->dhandle))
        return (0);

    /* If there are no subsequent WT_UPDATE structures we are done here. */
    // 这链表里没有连续的更新，此为第一更新，其实我理解为和插入差不多了
    if (upd->next == NULL || exclusive)
        return (0);

    /*
     * We would like to call __wt_txn_update_oldest only in the event that there are further updates
     * to this page, the check against WT_TXN_NONE is used as an indicator of there being further
     * updates on this page.
     ** 我们只想在此页面有进一步更新的情况下调用 __wt_txn_update_oldest，
     ** 对 WT_TXN_NONE 的检查用作此页面上有进一步更新的指示符。
     */
    if ((txn = page->modify->obsolete_check_txn) != WT_TXN_NONE) {
        obsolete_timestamp = page->modify->obsolete_check_timestamp;
        if (!__wt_txn_visible_all(session, txn, obsolete_timestamp)) {
            /* Try to move the oldest ID forward and re-check. */
            WT_RET(__wt_txn_update_oldest(session, 0));

            if (!__wt_txn_visible_all(session, txn, obsolete_timestamp))
                return (0);
        }

        page->modify->obsolete_check_txn = WT_TXN_NONE;
    }

    /* If we can't lock it, don't scan, that's okay. */
    // 如果得不到锁，其实也没啥问题
    if (WT_PAGE_TRYLOCK(session, page) != 0)
        return (0);

    obsolete = __wt_update_obsolete_check(session, cbt, upd->next, true);

    WT_PAGE_UNLOCK(session, page);

    __wt_free_update_list(session, &obsolete);

    return (0);
}
