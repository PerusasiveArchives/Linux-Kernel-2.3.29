#ifndef _LINUX_SEM_H
#define _LINUX_SEM_H

#include <linux/ipc.h>

/* semop flags */
#define SEM_UNDO        0x1000  /* undo the operation on exit */

/* semctl Command Definitions. */
#define GETPID  11       /* get sempid */
#define GETVAL  12       /* get semval */
#define GETALL  13       /* get all semval's */
#define GETNCNT 14       /* get semncnt */
#define GETZCNT 15       /* get semzcnt */
#define SETVAL  16       /* set semval */
#define SETALL  17       /* set all semval's */

/* ipcs ctl cmds */
#define SEM_STAT 18
#define SEM_INFO 19

/* One semid data structure for each set of semaphores in the system. */
struct semid_ds {
	struct ipc_perm	sem_perm;		/* permissions .. see ipc.h */
	__kernel_time_t	sem_otime;		/* last semop time */
	__kernel_time_t	sem_ctime;		/* last change time */
	struct sem	*sem_base;		/* ptr to first semaphore in array */
	struct sem_queue *sem_pending;		/* pending operations to be processed */
	struct sem_queue **sem_pending_last;	/* last pending operation */
	struct sem_undo	*undo;			/* undo requests on this array */
	unsigned short	sem_nsems;		/* no. of semaphores in array */
};

/* semop system calls takes an array of these. */
struct sembuf {
	unsigned short  sem_num;	/* semaphore index in array */
	short		sem_op;		/* semaphore operation */
	short		sem_flg;	/* operation flags */
};

/* arg for semctl system calls. */
union semun {
	int val;			/* value for SETVAL */
	struct semid_ds *buf;		/* buffer for IPC_STAT & IPC_SET */
	unsigned short *array;		/* array for GETALL & SETALL */
	struct seminfo *__buf;		/* buffer for IPC_INFO */
	void *__pad;
};

struct  seminfo {
	int semmap;
	int semmni;
	int semmns;
	int semmnu;
	int semmsl;
	int semopm;
	int semume;
	int semusz;
	int semvmx;
	int semaem;
};

#define SEMMNI  128             /* <= IPCMNI  max # of semaphore identifiers */
#define SEMMSL  250             /* <= 8 000 max num of semaphores per id */
#define SEMMNS  (SEMMNI*SEMMSL) /* <= INT_MAX max # of semaphores in system */
#define SEMOPM  32	        /* <= 1 000 max num of ops per semop call */
#define SEMVMX  32767           /* <= 32767 semaphore maximum value */

/* unused */
#define SEMUME  SEMOPM          /* max num of undo entries per process */
#define SEMMNU  SEMMNS          /* num of undo structures system wide */
#define SEMAEM  (SEMVMX >> 1)   /* adjust on exit max value */
#define SEMMAP  SEMMNS          /* # of entries in semaphore map */
#define SEMUSZ  20		/* sizeof struct sem_undo */

#ifdef __KERNEL__

/* One semaphore structure for each semaphore in the system. */
struct sem {
	int	semval;		/* current value */
	int	sempid;		/* pid of last operation */
};

/* One queue for each sleeping process in the system. */
struct sem_queue {
	struct sem_queue *	next;	 /* next entry in the queue */
	struct sem_queue **	prev;	 /* previous entry in the queue, *(q->prev) == q */
	struct task_struct*	sleeper; /* this process */
	struct sem_undo *	undo;	 /* undo structure */
	int    			pid;	 /* process id of requesting process */
	int    			status;	 /* completion status of operation */
	struct semid_ds *	sma;	 /* semaphore array for operations */
	int			id;	 /* internal sem id */
	struct sembuf *		sops;	 /* array of pending operations */
	int			nsops;	 /* number of operations */
	int			alter;	 /* operation will alter semaphore */
};

/* Each task has a list of undo requests. They are executed automatically
 * when the process exits.
 */
struct sem_undo {
	struct sem_undo *	proc_next;	/* next entry on this process */
	struct sem_undo *	id_next;	/* next entry on this semaphore set */
	int			semid;		/* semaphore set identifier */
	short *			semadj;		/* array of adjustments, one per semaphore */
};

asmlinkage long sys_semget (key_t key, int nsems, int semflg);
asmlinkage long sys_semop (int semid, struct sembuf *sops, unsigned nsops);
asmlinkage long sys_semctl (int semid, int semnum, int cmd, union semun arg);

#endif /* __KERNEL__ */

#endif /* _LINUX_SEM_H */
