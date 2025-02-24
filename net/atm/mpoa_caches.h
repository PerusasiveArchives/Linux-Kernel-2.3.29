#ifndef MPOA_CACHES_H
#define MPOA_CACHES_H

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/atmmpc.h>

struct mpoa_client;

void atm_mpoa_init_cache(struct mpoa_client *mpc);

typedef struct in_cache_entry {
        struct in_cache_entry *next;
        struct in_cache_entry *prev;
        struct timeval  tv;
        struct timeval  reply_wait;
        struct timeval  hold_down;
        uint32_t  packets_fwded;
        uint16_t  entry_state; 
        uint32_t retry_time;
        uint32_t refresh_time;
        uint32_t count;
        struct   atm_vcc *shortcut;
        uint8_t  MPS_ctrl_ATM_addr[ATM_ESA_LEN];
        struct   in_ctrl_info ctrl_info;
} in_cache_entry;

struct in_cache_ops{
        in_cache_entry *(*new_entry)(uint32_t dst_ip,
				     struct mpoa_client *client);
        in_cache_entry *(*search)(uint32_t dst_ip, struct mpoa_client *client);
        in_cache_entry *(*search_with_mask)(uint32_t dst_ip, 
					    struct mpoa_client *client,
					    uint32_t mask);
        in_cache_entry *(*search_by_vcc)(struct atm_vcc *vcc, 
                                         struct mpoa_client *client);
        int            (*cache_hit)(in_cache_entry *entry,
                                    struct mpoa_client *client);
        int            (*cache_remove)(in_cache_entry *delEntry,
                                        struct mpoa_client *client );
        void           (*clear_count)(struct mpoa_client *client);
        void           (*check_resolving)(struct mpoa_client *client);
        void           (*refresh)(struct mpoa_client *client);
};

typedef struct eg_cache_entry{
        struct               eg_cache_entry *next;
        struct               eg_cache_entry *prev;
        struct               timeval  tv;
        uint8_t              MPS_ctrl_ATM_addr[ATM_ESA_LEN];
        struct atm_vcc       *shortcut;
        uint32_t             packets_rcvd;
        uint16_t             entry_state;
        uint32_t             latest_ip_addr;    /* The src IP address of the last packet */
        struct eg_ctrl_info  ctrl_info;
} eg_cache_entry;

struct eg_cache_ops{
        eg_cache_entry *(*new_entry)(struct k_message *msg, struct mpoa_client *client);
        eg_cache_entry *(*search_by_cache_id)(uint32_t cache_id, struct mpoa_client *client);
        eg_cache_entry *(*search_by_tag)(uint32_t cache_id, struct mpoa_client *client);
        eg_cache_entry *(*search_by_vcc)(struct atm_vcc *vcc, struct mpoa_client *client);
        eg_cache_entry *(*search_by_src_ip)(uint32_t ipaddr, struct mpoa_client *client);
        int            (*cache_remove)(eg_cache_entry *entry, struct mpoa_client *client);
        void           (*update)(eg_cache_entry *entry, uint16_t holding_time);
        void           (*clear_expired)(struct mpoa_client *client);
};


/* Ingress cache entry states */

#define INGRESS_REFRESHING 3
#define INGRESS_RESOLVED   2
#define INGRESS_RESOLVING  1
#define INGRESS_INVALID    0

/* VCC states */

#define OPEN   1
#define CLOSED 0 

/* Egress cache entry states */

#define EGRESS_RESOLVED 2
#define EGRESS_PURGE    1
#define EGRESS_INVALID  0

#endif
