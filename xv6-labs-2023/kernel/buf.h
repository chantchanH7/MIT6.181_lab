struct buf {
  int valid;   // has data been read from disk? 表示buf是否为一个块的副本
  int disk;    // does disk "own" buf? 表示缓冲区已经交给了磁盘，磁盘可能会改变缓冲区的内容
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
//  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

