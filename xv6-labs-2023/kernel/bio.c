// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13 // 13 个哈希桶

struct {
    struct buf buf[NBUF]; // 缓冲区数组，30个buf
    struct spinlock lock; // 为整个 缓冲区数组维护一个锁
} bcache;

struct {
    struct spinlock bucket_locks[NBUCKETS];
    struct buf heads[NBUCKETS]; // hash桶头节点数组
} bucket;

static char* bcache_bucket_name[] = {
        "bcache_bucket_0",
        "bcache_bucket_1",
        "bcache_bucket_2",
        "bcache_bucket_3",
        "bcache_bucket_4",
        "bcache_bucket_5",
        "bcache_bucket_6",
        "bcache_bucket_7",
        "bcache_bucket_8",
        "bcache_bucket_9",
        "bcache_bucket_10",
        "bcache_bucket_11",
        "bcache_bucket_12",
};
/** 初始 struct */
//struct {
//  struct spinlock lock;
//  struct buf buf[NBUF];
//
//  // Linked list of all buffers, through prev/next.
//  // Sorted by how recently the buffer was used.
//  // head.next is most recent, head.prev is least.
//  struct buf head;
//} bcache;


/**
 * 一个简单的hash函数
 * */
int buf_hash(uint dev, uint blockno) {
    return (dev + blockno) % NBUCKETS;
}


void binit(void) {

    /** 首先 init bcache lock */
    initlock(&bcache.lock, "bcache_lock");

    /** 为每一个buffer 初始化它的 sleep lock */
    acquire(&bcache.lock);
    for(struct buf *b = bcache.buf; b < bcache.buf + NBUF; ++b) {
        initsleeplock(&b->lock, "bcache_buffer");
    }
    release(&bcache.lock);



    struct buf *b = bcache.buf;
    /** 首先为每个 hash bucket 初始化它的 spinlock */
    /** 然后将 buf 平均的分配给每一个 bucket 中 */
    for(int i = 0; i < NBUCKETS; ++i) {
        initlock(&bucket.bucket_locks[i], bcache_bucket_name[i]);
        bucket.heads[i].next = 0;
    }

    for(int i = 0; i < NBUCKETS; ++i) {
        for (int j = 0; j < NBUF / NBUCKETS; j++) {
            acquire(&bucket.bucket_locks[i]);
            b->blockno = i; // hash(b) should equal to i
            b->next = bucket.heads[i].next;
            bucket.heads[i].next = b;
            b++;
            release(&bucket.bucket_locks[i]);
        }
    }
}


/** 初始 binit */
//void
//binit(void)
//{
//  struct buf *b;
//
//  initlock(&bcache.lock, "bcache");
//
//  // Create linked list of buffers
//  bcache.head.prev = &bcache.head;
//  bcache.head.next = &bcache.head;
//  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
//    b->next = bcache.head.next;
//    b->prev = &bcache.head;
//    initsleeplock(&b->lock, "buffer");
//    bcache.head.next->prev = b;
//    bcache.head.next = b;
//  }
//}


/** 初始 binit */
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
//static struct buf*
//bget(uint dev, uint blockno)
//{
//  struct buf *b;
//
//  acquire(&bcache.lock);
//
//  // Is the block already cached?
//  for(b = bcache.head.next; b != &bcache.head; b = b->next){
//    if(b->dev == dev && b->blockno == blockno){
//      b->refcnt++;
//      release(&bcache.lock);
//      acquiresleep(&b->lock);
//      return b;
//    }
//  }
//
//  // Not cached.
//  // Recycle the least recently used (LRU) unused buffer.
//  // 分配buffer
//  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
//    if(b->refcnt == 0) {
//      b->dev = dev;
//      b->blockno = blockno;
//      b->valid = 0;
//      b->refcnt = 1;
//      release(&bcache.lock);
//      acquiresleep(&b->lock); // 请求睡眠锁
//      return b;
//    }
//  }
//  panic("bget: no buffers");
//}

static struct buf * bget(uint dev, uint blockno) {

    /** 先取到对应的 哈希值 */
    int index = buf_hash(dev, blockno);

    /** 接着对相应的 hash bucket 操作，这样的操作是需要上锁的 */
    acquire(&bucket.bucket_locks[index]);

    /**
     * 第一步:
     *  如果 hash bucket 中已经有 buf 且 buf 正好是对应 dev 的 block
     *  说明 the block is already cached
    **/
    struct buf *b;
    b = bucket.heads[index].next;
    while(b != 0) {
        if(b->blockno == blockno && b->dev == dev) { // 找到了
            ++(b->refcnt);  // 增加引用
            release(&bucket.bucket_locks[index]); // 释放锁
            acquiresleep(&b->lock);
            return b; // 返回
        }
        b = b->next; // 向下遍历
    }

    /**
     * 第二步:
     *  如果当前的hash bucket 中没有对应 dev 的 block 的 buf
     *  查看当前hash bucket 中是否有 空闲的 buf
     *  有就拿来用
     * */
    b = bucket.heads[index].next;
    while(b != 0) {
        if(b->refcnt == 0) {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            release(&bucket.bucket_locks[index]);
            acquiresleep(&b->lock);
            return b;
        }
        b = b->next;
    }


    release(&bucket.bucket_locks[index]); // 释放锁


    /**
     *  第三步:
     *  如果当前的 hash bucket 中确实没有，去其他 bucket 借一个
     * */
    struct buf * prev;
    struct buf * cur;
    b = 0;
    for(int i = 0; i < NBUCKETS; ++i) {
        if(i == index) { continue; } // 跳过自己
        acquire(&bucket.bucket_locks[i]); // 请求锁
        prev = &bucket.heads[i];
        cur = bucket.heads[i].next;
        while(cur != 0) {
            if(cur->refcnt == 0) { // 找到空闲的bucket了
                prev->next = cur->next; // 从之前的bucket中摘下来
                b = cur;
                b->dev = dev;
                b->blockno = blockno;
                b->valid = 0;
                b->refcnt = 1;
                release(&bucket.bucket_locks[i]);   // 释放锁
                acquiresleep(&b->lock);
                break; // 这个break跳出while循环
            }
            prev = cur;
            cur = cur->next;
        }
        // 如果在当前的bucket中没有找的空闲的buf，要及时释放bucket的锁
        if(b != 0) { break; } // 判断b是否为0，如果不为0，则证明找到了一个空闲的buf，可以跳出for循环
        else {
            release(&bucket.bucket_locks[i]);
        }
    }

    if(b != 0) {
        /**
         * 最后记得将 buf 添加在对应 bucket list 的链表上
         * */
        acquire(&bucket.bucket_locks[index]);
        b->next = bucket.heads[index].next;
        bucket.heads[index].next = b;
//        printf("func: bget > release(&bucket.bucket_locks[index]);\n");
        release(&bucket.bucket_locks[index]);
        return b;
    }

    /**
     * 第四步:
     *  如果都没有，就直接触发panic
     * */
    panic("bget: no buffers");
}



// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
    struct buf *b;

    b = bget(dev, blockno); // bget 现在缓存中寻找请求的块是否被缓存，如果没有，则剔除并返回一个缓存
    if(!b->valid) { // 返回的缓存为 invalid 表明我们需要在磁盘中读取该块
        virtio_disk_rw(b, 0); // 从磁盘中读取该块
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
    if(!holdingsleep(&b->lock)) // 如果当前进程或线程没有持有该缓冲区的锁，则触发panic
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

/** 初始 binit */
// Release a locked buffer.
// Move to the head of the most-recently-used list.
//void
//brelse(struct buf *b)
//{
//  if(!holdingsleep(&b->lock))
//    panic("brelse");
//
//  releasesleep(&b->lock);
//
//  acquire(&bcache.lock);
//  b->refcnt--;
//  if (b->refcnt == 0) {
//    // no one is waiting for it.
//    b->next->prev = b->prev;
//    b->prev->next = b->next;
//    b->next = bcache.head.next;
//    b->prev = &bcache.head;
//    bcache.head.next->prev = b;
//    bcache.head.next = b;
//  }
//
//  release(&bcache.lock);
//}

void brelse(struct buf * b) {
    if(!holdingsleep(&b->lock))
        panic("brelse");

//    printf("func: brelse >  releasesleep(&b->lock);\n");
    releasesleep(&b->lock);

    // 因为现在是释放 buf b，所以它必然已经在一个 hash bucket 中
    int index = buf_hash(b->dev, b->blockno);


    acquire(&bucket.bucket_locks[index]);
    --b->refcnt;
//    printf("func: brelse > release(&bucket.bucket_locks[index]);\n");
    release(&bucket.bucket_locks[index]);
}




void
bpin(struct buf *b) {

    int index = buf_hash(b->dev, b->blockno);
//    printf("func: bpin >  acquire(&bcache.bucket_locks[index]);\n");
    acquire(&bucket.bucket_locks[index]);
    b->refcnt++;
    release(&bucket.bucket_locks[index]);
}

void
bunpin(struct buf *b) {
    int index = buf_hash(b->dev, b->blockno);
//    printf("func: bunpin >  acquire(&bcache.bucket_locks[index]);\n");
    acquire(&bucket.bucket_locks[index]);
    b->refcnt--;
    release(&bucket.bucket_locks[index]);
}


