#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/statvfs.h>

#include <uthash.h>
#define FUSE_USE_VERSION 316
#include <fuse_lowlevel.h>
#include <linux/fuse.h>

#include "virtio.h"

#define MSIZE 8196

#define P9_NOFID UINT32_MAX

#define P9QTDIR     0x80

#define P9TVERSION 100
#define P9RVERSION 101
#define P9TATTACH  104
#define P9RATTACH  105
#define P9TWALK    110
#define P9RWALK    111
#define P9TREAD    116
#define P9RREAD    117
#define P9TCLUNK   120
#define P9RCLUNK   121

#define P9RLERROR   7
#define P9TSTATFS   8
#define P9RSTATFS   9
#define P9TLOPEN    12
#define P9RLOPEN    13
#define P9TREADLINK 22
#define P9RREADLINK 23
#define P9TGETATTR  24
#define P9RGETATTR  25
#define P9TREADDIR  40
#define P9RREADDIR  41

#define P9_GETATTR_MODE         0x00000001ULL
#define P9_GETATTR_NLINK        0x00000002ULL
#define P9_GETATTR_UID          0x00000004ULL
#define P9_GETATTR_GID          0x00000008ULL
#define P9_GETATTR_RDEV         0x00000010ULL
#define P9_GETATTR_ATIME        0x00000020ULL
#define P9_GETATTR_MTIME        0x00000040ULL
#define P9_GETATTR_CTIME        0x00000080ULL
#define P9_GETATTR_INO          0x00000100ULL
#define P9_GETATTR_SIZE         0x00000200ULL
#define P9_GETATTR_BLOCKS       0x00000400ULL
#define P9_GETATTR_BTIME        0x00000800ULL
#define P9_GETATTR_GEN          0x00001000ULL
#define P9_GETATTR_DATA_VERSION 0x00002000ULL
#define P9_GETATTR_BASIC        0x000007ffULL
#define P9_GETATTR_ALL          0x00003fffULL

struct virtio_9p_qid {
  uint8_t type;
  uint32_t version;
  uint64_t path;
} __attribute__((packed));

struct virtio_9p_msg {
  uint32_t size;
  uint8_t id;
  uint16_t tag;
  char data[MSIZE];
} __attribute__((packed));

struct virtio_9p_node {
  uint64_t qid;
  uint64_t nlookup;
  uint32_t fid;
  UT_hash_handle hh;
};

struct virtio_9p_dev {
  struct virtio_pci_dev virtio;
  struct virtio_9p_msg *msgs;
  uint32_t next_fid;
  uint32_t free_cap;
  uint32_t free_size;
  uint32_t *free_fid;
  uint64_t root_qid;
  struct virtio_9p_node *nodes;
};

static
size_t
align_down(size_t addr, size_t align) {
  return addr - addr % align;
}

static
size_t
align_up(size_t addr, size_t align) {
  return align_down(addr + align - 1, align);
}

static
void
encode_u16(struct virtio_9p_msg *msg, uint16_t value) {
  assert(msg->size + sizeof(value) <= sizeof(struct virtio_9p_msg));
  *(uint16_t *)(&msg->data[msg->size - offsetof(struct virtio_9p_msg, data)]) = value;
  msg->size += sizeof(value);
}

static
void
encode_u32(struct virtio_9p_msg *msg, uint32_t value) {
  assert(msg->size + sizeof(value) <= sizeof(struct virtio_9p_msg));
  *(uint32_t *)(&msg->data[msg->size - offsetof(struct virtio_9p_msg, data)]) = value;
  msg->size += sizeof(value);
}

static
void
encode_u64(struct virtio_9p_msg *msg, uint64_t value) {
  assert(msg->size + sizeof(value) <= sizeof(struct virtio_9p_msg));
  *(uint64_t *)(&msg->data[msg->size - offsetof(struct virtio_9p_msg, data)]) = value;
  msg->size += sizeof(value);
}

static
void
encode_string(struct virtio_9p_msg *msg, char const *s) {
  size_t size = strlen(s);
  assert(size <= UINT16_MAX);
  encode_u16(msg, size);
  assert(msg->size + size <= sizeof(struct virtio_9p_msg));
  memcpy(&msg->data[msg->size - offsetof(struct virtio_9p_msg, data)], s, size);
  msg->size += size;
}

static
uint32_t
decode_u8(struct virtio_9p_msg *msg, uint32_t offset, uint8_t *value) {
  assert(offset + sizeof(*value) <= msg->size);
  *value = msg->data[offset];
  return offset + sizeof(*value);
}

static
uint32_t
decode_u16(struct virtio_9p_msg *msg, uint32_t offset, uint16_t *value) {
  assert(offset + sizeof(*value) <= msg->size);
  *value = *(uint16_t *)(&msg->data[offset]);
  return offset + sizeof(*value);
}

static
uint32_t
decode_u32(struct virtio_9p_msg *msg, uint32_t offset, uint32_t *value) {
  assert(offset + sizeof(*value) <= msg->size);
  *value = *(uint32_t *)(&msg->data[offset]);
  return offset + sizeof(*value);
}

static
uint32_t
decode_u64(struct virtio_9p_msg *msg, uint32_t offset, uint64_t *value) {
  assert(offset + sizeof(*value) <= msg->size);
  *value = *(uint64_t *)(&msg->data[offset]);
  return offset + sizeof(*value);
}

static
uint32_t
decode_qid(struct virtio_9p_msg *msg, uint32_t offset, struct virtio_9p_qid *qid) {
  offset = decode_u8(msg, offset, &qid->type);
  offset = decode_u32(msg, offset, &qid->version);
  offset = decode_u64(msg, offset, &qid->path);
  return offset;
}

static
uint16_t
decode_string(struct virtio_9p_msg *msg, uint32_t offset, uint16_t *len, char const **s) {
  uint16_t size = 0;
  offset = decode_u16(msg, offset, &size);
  assert(offset + size <= msg->size);
  *len = size;
  *s = &msg->data[offset];
  return offset + size;
}

static
uint64_t
virtio_9p_fix_qid(struct virtio_9p_dev *dev, uint64_t qid) {
  if (qid == dev->root_qid)
    return 1;
  if (qid == 1)
    return dev->root_qid;
  return qid;
}

static
uint32_t
virtio_9p_alloc_fid(struct virtio_9p_dev *dev) {
  if (dev->free_size > 0) {
    --(dev->free_size);
    return dev->free_fid[dev->free_size];
  }
  uint32_t fid = dev->next_fid;
  ++(dev->next_fid);
  return fid;
}

static
void
virtio_9p_free_fid(struct virtio_9p_dev *dev, uint32_t fid) {
  if (dev->free_size == dev->free_cap) {
    dev->free_cap = dev->free_cap * 2;
    if (dev->free_cap == 0)
      dev->free_cap = 4;

    uint32_t *free_fid = realloc(dev->free_fid, sizeof(uint32_t) * dev->free_cap);
    assert(free_fid != NULL);
    dev->free_fid = free_fid;
  }

  dev->free_fid[dev->free_size] = fid;
  ++(dev->free_size);
}

static
uint16_t
virtio_9p_alloc(struct virtio_9p_dev *dev, struct virtio_9p_msg **request, struct virtio_9p_msg **response) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  int req_id = virtio_queue_alloc(queue, 2);
  assert(req_id >= 0);
  uint32_t res_id = queue->vring.desc[req_id].next;

  *request = &dev->msgs[req_id];
  *response = &dev->msgs[res_id];

  dev->msgs[req_id].size = 7;
  dev->msgs[req_id].tag = req_id;
  return req_id;
}

static
void
virtio_9p_free(struct virtio_9p_dev *dev, uint16_t id) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  virtio_queue_free(queue, id);
}

static
int
virtio_9p_sendrecv(struct virtio_9p_dev *dev, uint16_t id, struct virtio_9p_msg *request, struct virtio_9p_msg *response) {
  struct virtio_queue *queue = &dev->virtio.queues[0];
  struct iovec iov[] = {
    { .iov_base = request,
      .iov_len = sizeof(struct virtio_9p_msg)},
    { .iov_base = response,
      .iov_len = sizeof(struct virtio_9p_msg)}
  };

  virtio_queue_writev(queue, id, iov, 2, 1);
  virtio_queue_send(queue, id);

  struct vring_used_elem *elem = NULL;

  while (elem == NULL) {
    vfio_pci_dev_clear_irq(dev->virtio.pci, 0);
    virtio_queue_handle_event(queue);
    elem = virtio_queue_recv(queue);
  }

  assert(elem->id == id);
  assert(response->tag == request->tag);

  if (response->id == request->id + 1)
    return 0;

  assert(response->id == P9RLERROR);

  uint32_t offset = 0;
  uint32_t ecode;
  offset = decode_u32(response, offset, &ecode);

  assert(offset + 7 == response->size);
  virtio_9p_free(dev, id);
  return -ecode;
}

static
void
virtio_9p_version(struct virtio_9p_dev *dev, char const *version) {
  struct virtio_9p_msg *request = NULL;
  struct virtio_9p_msg *response = NULL;
  uint16_t id = virtio_9p_alloc(dev, &request, &response);
  request->id = P9TVERSION;

  encode_u32(request, sizeof(struct virtio_9p_msg));
  encode_string(request, version);

  assert(virtio_9p_sendrecv(dev, id, request, response) == 0);

  uint32_t offset = 0;
  uint32_t msize = 0;
  uint16_t len = 0;
  char const *s = NULL;

  offset = decode_u32(response, offset, &msize);
  offset = decode_string(response, offset, &len, &s);
  assert(msize == sizeof(struct virtio_9p_msg));
  assert(strncmp(version, s, len) == 0);

  assert(offset + 7 == response->size);
  virtio_9p_free(dev, id);
}

static
void
virtio_9p_attach(struct virtio_9p_dev *dev, uint32_t fid, struct virtio_9p_qid *qid) {
  struct virtio_9p_msg *request = NULL;
  struct virtio_9p_msg *response = NULL;
  uint16_t id = virtio_9p_alloc(dev, &request, &response);
  request->id = P9TATTACH;

  encode_u32(request, fid);
  encode_u32(request, P9_NOFID);
  encode_string(request, "root");
  encode_string(request, "");

  assert(virtio_9p_sendrecv(dev, id, request, response) == 0);

  uint32_t offset = 0;
  offset = decode_qid(response, offset, qid);

  assert(offset + 7 == response->size);
  virtio_9p_free(dev, id);
}

static
int
virtio_9p_walk(struct virtio_9p_dev *dev, uint32_t fid, uint32_t newfid, char const *name, struct virtio_9p_qid *qid) {
  struct virtio_9p_msg *request = NULL;
  struct virtio_9p_msg *response = NULL;
  uint16_t id = virtio_9p_alloc(dev, &request, &response);
  request->id = P9TWALK;

  encode_u32(request, fid);
  encode_u32(request, newfid);
  if (name == NULL) {
    encode_u16(request, 0);
  } else {
    encode_u16(request, 1);
    encode_string(request, name);
  }

  int result = virtio_9p_sendrecv(dev, id, request, response);
  if (result < 0)
    return result;

  uint32_t offset = 0;
  uint16_t n = 0;
  offset = decode_u16(response, offset, &n);

  if (name != NULL)
    offset = decode_qid(response, offset, qid);

  assert(offset + 7 == response->size);
  virtio_9p_free(dev, id);
  return 0;
}

static
int
virtio_9p_clunk(struct virtio_9p_dev *dev, uint32_t fid) {
  struct virtio_9p_msg *request = NULL;
  struct virtio_9p_msg *response = NULL;
  uint16_t id = virtio_9p_alloc(dev, &request, &response);
  request->id = P9TCLUNK;

  encode_u32(request, fid);

  int result = virtio_9p_sendrecv(dev, id, request, response);
  if (result < 0)
    return result;

  virtio_9p_free_fid(dev, fid);

  uint32_t offset = 0;

  assert(offset + 7 == response->size);
  virtio_9p_free(dev, id);
  return 0;
}

static
int
virtio_9p_statfs(struct virtio_9p_dev *dev, uint32_t fid, struct statvfs *st) {
  struct virtio_9p_msg *request = NULL;
  struct virtio_9p_msg *response = NULL;
  uint16_t id = virtio_9p_alloc(dev, &request, &response);
  request->id = P9TSTATFS;

  encode_u32(request, fid);

  int result = virtio_9p_sendrecv(dev, id, request, response);
  if (result < 0)
    return result;

  uint32_t offset = 0;
  uint32_t type;
  uint32_t bsize;
  uint64_t blocks;
  uint64_t bfree;
  uint64_t bavail;
  uint64_t files;
  uint64_t ffree;
  uint64_t fsid;
  uint32_t namelen;

  offset = decode_u32(response, offset, &type);
  offset = decode_u32(response, offset, &bsize);
  offset = decode_u64(response, offset, &blocks);
  offset = decode_u64(response, offset, &bfree);
  offset = decode_u64(response, offset, &bavail);
  offset = decode_u64(response, offset, &files);
  offset = decode_u64(response, offset, &ffree);
  offset = decode_u64(response, offset, &fsid);
  offset = decode_u32(response, offset, &namelen);
  assert(offset + 7 == response->size);
  virtio_9p_free(dev, id);

  st->f_bsize = bsize;
  st->f_frsize = bsize;
  st->f_blocks = blocks;
  st->f_bfree = bfree;
  st->f_bavail = bavail;
  st->f_files = files;
  st->f_ffree = ffree;
  st->f_fsid = fsid;
  st->f_namemax = namelen;

  return 0;
}

static
int
virtio_9p_lopen(struct virtio_9p_dev *dev, uint32_t fid, uint32_t flags) {
  struct virtio_9p_msg *request = NULL;
  struct virtio_9p_msg *response = NULL;
  uint16_t id = virtio_9p_alloc(dev, &request, &response);
  request->id = P9TLOPEN;

  encode_u32(request, fid);
  encode_u32(request, flags);

  int result = virtio_9p_sendrecv(dev, id, request, response);
  if (result < 0)
    return result;

  virtio_9p_free(dev, id);
  return 0;
}

static
int
virtio_9p_getattr(struct virtio_9p_dev *dev, uint32_t fid, struct stat *st) {
  struct virtio_9p_msg *request = NULL;
  struct virtio_9p_msg *response = NULL;
  uint16_t id = virtio_9p_alloc(dev, &request, &response);
  request->id = P9TGETATTR;

  encode_u32(request, fid);
  encode_u64(request, P9_GETATTR_BASIC);

  int result = virtio_9p_sendrecv(dev, id, request, response);
  if (result < 0)
    return result;

  uint32_t offset = 0;
  uint64_t valid = 0;
  offset = decode_u64(response, offset, &valid);

  struct virtio_9p_qid qid;
  offset = decode_qid(response, offset, &qid);

  if (valid & P9_GETATTR_INO)
    st->st_ino = virtio_9p_fix_qid(dev, qid.path);

  uint32_t mode;
  offset = decode_u32(response, offset, &mode);
  if (valid & P9_GETATTR_MODE)
    st->st_mode = mode;

  uint32_t uid;
  offset = decode_u32(response, offset, &uid);
  if (valid & P9_GETATTR_UID)
    st->st_uid = uid;

  uint32_t gid;
  offset = decode_u32(response, offset, &gid);
  if (valid & P9_GETATTR_GID)
    st->st_gid = gid;

  uint64_t nlink;
  offset = decode_u64(response, offset, &nlink);
  if (valid & P9_GETATTR_NLINK)
    st->st_nlink = nlink;

  uint64_t rdev;
  offset = decode_u64(response, offset, &rdev);
  if (valid & P9_GETATTR_RDEV)
    st->st_rdev = rdev;

  uint64_t size;
  offset = decode_u64(response, offset, &size);
  if (valid & P9_GETATTR_SIZE)
    st->st_size = size;

  uint64_t blksize;
  offset = decode_u64(response, offset, &blksize);
  st->st_blksize = blksize;

  uint64_t blocks;
  offset = decode_u64(response, offset, &blocks);

  if (valid & P9_GETATTR_BLOCKS)
    st->st_blocks = blocks;

  uint64_t sec;
  uint64_t nsec;
  offset = decode_u64(response, offset, &sec);
  offset = decode_u64(response, offset, &nsec);
  if (valid & P9_GETATTR_ATIME) {
    st->st_atim.tv_sec = sec;
    st->st_atim.tv_nsec = nsec;
  }

  offset = decode_u64(response, offset, &sec);
  offset = decode_u64(response, offset, &nsec);
  if (valid & P9_GETATTR_MTIME) {
    st->st_mtim.tv_sec = sec;
    st->st_mtim.tv_nsec = nsec;
  }

  offset = decode_u64(response, offset, &sec);
  offset = decode_u64(response, offset, &nsec);
  if (valid & P9_GETATTR_CTIME) {
    st->st_ctim.tv_sec = sec;
    st->st_ctim.tv_nsec = nsec;
  }

  offset = decode_u64(response, offset, &sec);
  offset = decode_u64(response, offset, &nsec);
  if (valid & P9_GETATTR_BTIME) {
  }

  uint64_t gen;
  offset = decode_u64(response, offset, &gen);

  uint64_t data_version;
  offset = decode_u64(response, offset, &data_version);

  assert(offset + 7 == response->size);
  virtio_9p_free(dev, id);
  return 0;
}

static
ssize_t
virtio_9p_readdir(struct virtio_9p_dev *dev, uint32_t fid, off_t off, size_t size, char *buf) {
  struct virtio_9p_msg *request = NULL;
  struct virtio_9p_msg *response = NULL;
  uint16_t id = virtio_9p_alloc(dev, &request, &response);
  request->id = P9TREADDIR;

  encode_u32(request, fid);
  encode_u64(request, off);
  encode_u32(request, 4096);

  int result = virtio_9p_sendrecv(dev, id, request, response);
  if (result < 0)
    return result;

  uint32_t offset = 0;
  uint32_t count = 0;

  offset = decode_u32(response, offset, &count);

  char *p = buf;
  while (offset + 7 < response->size) {
    struct virtio_9p_qid qid;
    uint64_t off;
    uint8_t type;
    uint16_t len;
    char const *s;

    offset = decode_qid(response, offset, &qid);
    offset = decode_u64(response, offset, &off);
    offset = decode_u8(response, offset, &type);
    offset = decode_string(response, offset, &len, &s);

    size_t entsize = FUSE_NAME_OFFSET + len;
    if (size < FUSE_DIRENT_ALIGN(entsize))
      break;

    struct fuse_dirent *ent = (struct fuse_dirent *)p;
    ent->ino = virtio_9p_fix_qid(dev, qid.path);
    ent->off = off;
    ent->namelen = len;
    ent->type = type;

    memcpy(ent->name, s, len);
    memset(p+entsize, 0, FUSE_DIRENT_ALIGN(entsize)-entsize);

    p += FUSE_DIRENT_ALIGN(entsize);
  }

  assert(offset + 7 == response->size);
  virtio_9p_free(dev, id);
  return p - buf;
}

static
void
virtio_9p_dev_init(struct virtio_9p_dev *dev, struct vfio_pci_dev *pci) {
  virtio_pci_dev_init(&dev->virtio, pci, 0);
  struct virtio_queue *queue = &dev->virtio.queues[0];
  size_t size = sizeof(struct virtio_9p_msg) * queue->vring.num;
  dev->msgs = vfio_pci_dev_map_dma(pci, NULL, align_up(size, 4096), -1, 0);
  virtio_send_driver_ok(&dev->virtio);
  dev->next_fid = 0;
  dev->free_cap = 0;
  dev->free_size = 0;
  dev->free_fid = NULL;

  virtio_9p_version(dev, "9P2000.L");
  struct virtio_9p_qid qid;
  uint32_t fid = virtio_9p_alloc_fid(dev);
  virtio_9p_attach(dev, fid, &qid);
  assert(qid.type == P9QTDIR);
  dev->root_qid = qid.path;

  dev->nodes = NULL;

  struct virtio_9p_node *node = malloc(sizeof(struct virtio_9p_node));
  assert(node != NULL);
  node->qid = 1;
  node->nlookup = 1;
  node->fid = fid;
  HASH_ADD(hh, dev->nodes, qid, sizeof(uint64_t), node);
}

static
void
virtio_9p_op_init(void *userdata, struct fuse_conn_info *conn) {
  conn->max_read = 8192;
  conn->max_write = 8192;
}

static
void
virtio_9p_op_lookup(fuse_req_t req, fuse_ino_t ino, char const *name) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);
  struct virtio_9p_node *node = NULL;
  HASH_FIND(hh, dev->nodes, &ino, sizeof(uint64_t), node);
  assert(node != NULL);

  uint32_t newfid = virtio_9p_alloc_fid(dev);
  struct virtio_9p_qid qid;
  int result = virtio_9p_walk(dev, node->fid, newfid, name, &qid);
  if (result < 0) {
    virtio_9p_free_fid(dev, newfid);
    fuse_reply_err(req, -result);
    return;
  }

  struct fuse_entry_param e = {0};

  uint64_t nodeid = virtio_9p_fix_qid(dev, qid.path);
  HASH_FIND(hh, dev->nodes, &nodeid, sizeof(uint64_t), node);
  if (node != NULL) {
    assert(virtio_9p_clunk(dev, newfid) == 0);
    result = virtio_9p_getattr(dev, node->fid, &e.attr);
    if (result < 0) {
      fuse_reply_err(req, -result);
    } else {
      ++(node->nlookup);
      e.ino = nodeid;
      e.attr_timeout = 1.0;
      e.entry_timeout = 1.0;
      fuse_reply_entry(req, &e);
    }
    return;
  }

  result = virtio_9p_getattr(dev, newfid, &e.attr);
  if (result < 0) {
    assert(virtio_9p_clunk(dev, newfid) == 0);
    fuse_reply_err(req, -result);
    return;
  }

  node = malloc(sizeof(struct virtio_9p_node));
  assert(node != NULL);
  node->qid = nodeid;
  node->nlookup = 1;
  node->fid = newfid;
  HASH_ADD(hh, dev->nodes, qid, sizeof(uint64_t), node);

  e.ino = nodeid;
  e.attr_timeout = 1.0;
  e.entry_timeout = 1.0;
  fuse_reply_entry(req, &e);
}

static
void
virtio_9p_op_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);
  struct virtio_9p_node *node = NULL;
  HASH_FIND(hh, dev->nodes, &ino, sizeof(uint64_t), node);
  assert(node != NULL);

  assert(node->nlookup >= nlookup);
  node->nlookup -= nlookup;

  fuse_reply_none(req);
  if (node->nlookup > 0)
    return;

  HASH_DEL(dev->nodes, node);
  assert(virtio_9p_clunk(dev, node->fid) == 0);
  free(node);
}

static
void
virtio_9p_op_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);
  struct virtio_9p_node *node = NULL;
  HASH_FIND(hh, dev->nodes, &ino, sizeof(uint64_t), node);
  assert(node != NULL);

  struct stat st = {0};
  int result = virtio_9p_getattr(dev, node->fid, &st);
  if (result < 0) {
    fuse_reply_err(req, -result);
    return;
  }
  fuse_reply_attr(req, &st, 1.0);
}

static
void
virtio_9p_op_readlink(fuse_req_t req, fuse_ino_t ino) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);
  struct virtio_9p_node *node = NULL;
  HASH_FIND(hh, dev->nodes, &ino, sizeof(uint64_t), node);
  assert(node != NULL);

  struct virtio_9p_msg *request = NULL;
  struct virtio_9p_msg *response = NULL;
  uint16_t id = virtio_9p_alloc(dev, &request, &response);
  request->id = P9TREADLINK;

  encode_u32(request, node->fid);

  int result = virtio_9p_sendrecv(dev, id, request, response);
  if (result < 0) {
    fuse_reply_err(req, -result);
    return;
  }

  uint32_t offset = 0;
  uint16_t len = 0;
  char const *s = NULL;
  offset = decode_string(response, offset, &len, &s);
  assert(offset + 7 == response->size);

  fuse_reply_buf(req, s, len);
  virtio_9p_free(dev, id);
}


static
void
virtio_9p_op_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);
  struct virtio_9p_node *node = NULL;
  HASH_FIND(hh, dev->nodes, &ino, sizeof(uint64_t), node);
  assert(node != NULL);

  uint32_t newfid = virtio_9p_alloc_fid(dev);
  int result = virtio_9p_walk(dev, node->fid, newfid, NULL, NULL);
  if (result < 0) {
    virtio_9p_free_fid(dev, newfid);
    fuse_reply_err(req, -result);
    return;
  }

  result = virtio_9p_lopen(dev, newfid, fi->flags);
  if (result < 0) {
    assert(virtio_9p_clunk(dev, newfid) == 0);
    fuse_reply_err(req, -result);
    return;
  }

  fi->fh = newfid;
  fuse_reply_open(req, fi);
}

static
void
virtio_9p_op_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);
  assert(fi->fh != 0);

  struct virtio_9p_msg *request = NULL;
  struct virtio_9p_msg *response = NULL;
  uint16_t id = virtio_9p_alloc(dev, &request, &response);
  request->id = P9TREAD;

  encode_u32(request, fi->fh);
  encode_u64(request, off);
  encode_u32(request, size);

  int result = virtio_9p_sendrecv(dev, id, request, response);
  if (result < 0) {
    fuse_reply_err(req, -result);
    return;
  }

  uint32_t offset = 0;
  uint32_t count = 0;
  offset = decode_u32(response, offset, &count);
  assert(offset + 7 + count == response->size);

  fuse_reply_buf(req, &response->data[offset], count);
  virtio_9p_free(dev, id);
}

static
void
virtio_9p_op_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);
  assert(fi->fh != 0);

  ssize_t result = virtio_9p_clunk(dev, fi->fh);
  if (result < 0) {
    fuse_reply_err(req, -result);
    return;
  }
  fuse_reply_err(req, 0);
}

static
void
virtio_9p_op_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);
  struct virtio_9p_node *node = NULL;
  HASH_FIND(hh, dev->nodes, &ino, sizeof(uint64_t), node);
  assert(node != NULL);

  uint32_t newfid = virtio_9p_alloc_fid(dev);
  int result = virtio_9p_walk(dev, node->fid, newfid, NULL, NULL);
  if (result < 0) {
    virtio_9p_free_fid(dev, newfid);
    fuse_reply_err(req, -result);
    return;
  }

  result = virtio_9p_lopen(dev, newfid, O_RDONLY | O_DIRECTORY);
  if (result < 0) {
    assert(virtio_9p_clunk(dev, newfid) == 0);
    fuse_reply_err(req, -result);
    return;
  }

  fi->fh = newfid;
  fuse_reply_open(req, fi);
}

static
void
virtio_9p_op_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);
  assert(fi->fh != 0);

  char *buf = malloc(size);
  assert(buf != NULL);

  ssize_t result = virtio_9p_readdir(dev, fi->fh, off, size, buf);
  if (result < 0) {
    fuse_reply_err(req, -result);
  } else {
    fuse_reply_buf(req, buf, result);
  }
  free(buf);
}

static
void
virtio_9p_op_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);
  assert(fi->fh != 0);

  ssize_t result = virtio_9p_clunk(dev, fi->fh);
  if (result < 0) {
    fuse_reply_err(req, -result);
    return;
  }
  fuse_reply_err(req, 0);
}

static
void
virtio_9p_op_statfs(fuse_req_t req, fuse_ino_t ino) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);
  struct virtio_9p_node *node = NULL;
  HASH_FIND(hh, dev->nodes, &ino, sizeof(uint64_t), node);
  assert(node != NULL);

  struct statvfs st;
  int result = virtio_9p_statfs(dev, node->fid, &st);
  if (result < 0) {
    fuse_reply_err(req, -result);
    return;
  }

  fuse_reply_statfs(req, &st);
}

static
void
virtio_9p_op_forget_multi(fuse_req_t req, size_t count, struct fuse_forget_data *forgets) {
  struct virtio_9p_dev *dev = fuse_req_userdata(req);

  for (size_t i=0; i<count; ++i) {
    struct virtio_9p_node *node = NULL;
    HASH_FIND(hh, dev->nodes, &forgets[i].ino, sizeof(uint64_t), node);
    assert(node != NULL);

    assert(node->nlookup >= forgets[i].nlookup);
    node->nlookup -= forgets[i].nlookup;

    if (node->nlookup > 0)
      continue;

    HASH_DEL(dev->nodes, node);
    assert(virtio_9p_clunk(dev, node->fid) == 0);
    free(node);
  }

  fuse_reply_none(req);
}

static const
struct fuse_lowlevel_ops ops = {
  .init = virtio_9p_op_init,
  .destroy = NULL,
  .lookup = virtio_9p_op_lookup,
  .forget = virtio_9p_op_forget,
  .getattr = virtio_9p_op_getattr,
  .setattr = NULL,
  .readlink = virtio_9p_op_readlink,
  .mknod = NULL,
  .mkdir = NULL,
  .unlink = NULL,
  .rmdir = NULL,
  .symlink = NULL,
  .rename = NULL,
  .link = NULL,
  .open = virtio_9p_op_open,
  .read = virtio_9p_op_read,
  .write = NULL,
  .flush = NULL,
  .release = virtio_9p_op_release,
  .fsync = NULL,
  .opendir = virtio_9p_op_opendir,
  .readdir = virtio_9p_op_readdir,
  .releasedir = virtio_9p_op_releasedir,
  .fsyncdir = NULL,
  .statfs = virtio_9p_op_statfs,
  .setxattr = NULL,
  .getxattr = NULL,
  .listxattr = NULL,
  .removexattr = NULL,
  .access = NULL,
  .create = NULL,
  .getlk = NULL,
  .setlk = NULL,
  .bmap = NULL,
  .ioctl = NULL,
  .poll = NULL,
  .write_buf = NULL,
  .retrieve_reply = NULL,
  .forget_multi = virtio_9p_op_forget_multi,
  .flock = NULL,
  .fallocate = NULL,
  .readdirplus = NULL,
  .copy_file_range = NULL,
  .lseek = NULL,
};

int
main(int argc, char **argv) {
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  struct fuse_cmdline_opts opts;

  assert(fuse_parse_cmdline(&args, &opts) == 0);

  if (opts.show_help) {
    printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
    fuse_cmdline_help();
    fuse_lowlevel_help();
    exit(0);
  }

  assert(opts.mountpoint != NULL);
  fuse_opt_add_arg(&args, "-o");
  fuse_opt_add_arg(&args, "max_read=8192,ro");

  struct vfio_pci_dev pci = {0};
  char const *devid = getenv("DEVID");
  if (devid == NULL)
    devid = "0000:00:07.0";

  vfio_pci_dev_open(devid, &pci);
  vfio_pci_dev_init(&pci);

  struct virtio_9p_dev dev = {0};
  virtio_9p_dev_init(&dev, &pci);

  struct fuse_session *se = fuse_session_new(&args, &ops, sizeof(ops), &dev);
  assert(se != NULL);

  assert(fuse_session_mount(se, opts.mountpoint) == 0);
  return fuse_session_loop(se);
}
