// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
#ifdef LAB_LOCK
  int nts;           // 由于尝试获取另一个核心已持有的锁而在acquire中进行的循环迭代次数
  int n;             // 对该锁的acquire调用次数
#endif
};

