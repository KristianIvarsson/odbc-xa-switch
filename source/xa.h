#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define XIDDATASIZE 128

typedef struct 
{  
    long formatID{-1};
    long gtrid_length;
    long bqual_length;
    char data[ XIDDATASIZE];
} XID;

#define TMNOFLAGS      0x00000000L
#define TMMIGRATE      0x00100000L
#define TMJOIN         0x00200000L
#define TMENDRSCAN     0x00800000L
#define TMSTARTRSCAN   0x01000000L
#define TMSUSPEND      0x02000000L
#define TMSUCCESS      0x04000000L
#define TMRESUME       0x08000000L
#define TMFAIL         0x20000000L
#define TMONEPHASE     0x40000000L

#define XA_OK          0
#define XA_RDONLY      3
#define XA_RETRY       4

#define XAER_ASYNC    -2
#define XAER_RMERR    -3
#define XAER_NOTA     -4
#define XAER_INVAL    -5
#define XAER_PROTO    -6
#define XAER_RMFAIL   -7
#define XAER_DUPID    -8
#define XAER_OUTSIDE  -9

typedef struct xa_switch_t 
{
    char name[ 32];
    long flags;
    long version;
    int (*xa_open_entry)( char* xa_info, int rmid, long flags);
    int (*xa_close_entry)( char* xa_info, int rmid, long flags);
    int (*xa_start_entry)( XID* xid, int rmid, long flags);
    int (*xa_end_entry)( XID* xid, int rmid, long flags);
    int (*xa_rollback_entry)( XID* xid, int rmid, long flags);
    int (*xa_prepare_entry)( XID* xid, int rmid, long flags);
    int (*xa_commit_entry)( XID* xid, int rmid, long flags);
    int (*xa_recover_entry)( XID* xids, long count, int rmid, long flags);
    int (*xa_forget_entry)( XID* xid, int rmid, long flags);
    int (*xa_complete_entry)( int* handle, int* retval, int rmid, long flags);
} xa_switch_t;

#ifdef __cplusplus
}
#endif
