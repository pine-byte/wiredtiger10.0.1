/*
 * @Author: chenqingsong chenqingsong@kingsoft.com
 * @Date: 2022-09-02 11:37:55
 * @LastEditors: chenqingsong chenqingsong@kingsoft.com
 * @LastEditTime: 2022-11-16 15:46:22
 * @FilePath: /wiredtiger/wt_demo.c  代码文件
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "wiredtiger.h"
// #include <string.h>
#include <stdio.h>
#include <unistd.h>

/**
 * @description: main no args
 * @return {*}
 */
int main(){
    char* home="/tmp/wt";
    WT_CONNECTION* conn;
    WT_SESSION* session;
    WT_CURSOR* cursor;
    char key[20], value[20];
    int ret;
    wiredtiger_open(home, NULL,
        "create,log=(enabled,recover=on),eviction=(threads_max=2,threads_min=1),statistics=(all),"
        // "verbose=[api,backup,block,block_cache,checkpoint,checkpoint_cleanup,checkpoint_progress,"
        // "compact,compact_progress,error_returns,evict,evict_stuck,evictserver,fileops,"
        // "handleops,history_store,history_store_activity,log,lsm,"
        // "lsm_manager,metadata,mutex,overflow,read,reconcile,"
        // "recovery,recovery_progress,rts,salvage,shared_cache,split,"
        // "temporary,thread_group,tiered,timestamp,transaction,"
        // "verify,version,write"]"
        , &conn);
    // sleep(100000);
    conn->open_session(conn, NULL, NULL, &session);
    session->create(session, "table:demo", "key_format=S,value_format=S");
    session->open_cursor(session, "table:demo", NULL, NULL, &cursor);
    for(int i=1;i<10000;i++){
        sprintf(key, "key%d", i);
        sprintf(value, "value%d", i);
        ret = session->begin_transaction(session, NULL);
        cursor->set_key(cursor, key);
        cursor->set_value(cursor, value);
        switch (ret = cursor->insert(cursor)) {
            case 0:                                 /* Update success __session_commit_transaction */
                ret = session->commit_transaction(session, NULL);
            /*
                * If commit_transaction succeeds, cursors remain positioned; if
                * commit_transaction fails, the transaction was rolled-back and
                * and all cursors are reset.
                **  如果提交成功，游标保持当前位置；否则，
                **  事务被回滚并且所有游标被重置。
                */
            break;
            case WT_ROLLBACK:                       /* Update conflict */
            default:                                /* Other error */
                ret = session->rollback_transaction(session, NULL);
                /* The rollback_transaction call resets all cursors. */
                break;
        }

        // 触发第二次修改
        ret = session->begin_transaction(session, NULL);
        sprintf(key, "key%d", i);
        sprintf(value, "value%d-update", i);
        cursor->set_key(cursor, key);
        cursor->set_value(cursor, value);
                switch (ret = cursor->insert(cursor)) {
            case 0:                                 /* Update success */
                ret = session->commit_transaction(session, NULL);
            /*
                * If commit_transaction succeeds, cursors remain positioned; if
                * commit_transaction fails, the transaction was rolled-back and
                * and all cursors are reset.
                */
            break;
            case WT_ROLLBACK:                       /* Update conflict */
            default:                                /* Other error */
                ret = session->rollback_transaction(session, NULL);
                /* The rollback_transaction call resets all cursors. */
                break;
        }

        // sleep(1);
        if(i%1000 == 0){
            session->checkpoint(session, NULL);
        }
    }

    cursor->reset(cursor);
    ret = cursor->next(cursor);
    while(ret == 0){
        cursor->get_key(cursor, &key);
        cursor->get_value(cursor, &value);
        printf("get record: %s, %s \n", key, value);
        ret = cursor->next(cursor);
    }
    conn->close(conn, NULL);
    return 0;
}
