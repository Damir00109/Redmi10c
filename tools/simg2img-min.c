/* Minimal Android sparse → raw for TWRP (static aarch64). */
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SPARSE_MAGIC 0xed26ff3a
#define CHUNK_RAW 0xCAC1
#define CHUNK_FILL 0xCAC2
#define CHUNK_DONT_CARE 0xCAC3
#define CHUNK_CRC32 0xCAC4

#pragma pack(push, 1)
struct sparse_header {
  uint32_t magic;
  uint16_t major, minor;
  uint16_t file_hdr_sz, chunk_hdr_sz;
  uint32_t blk_sz, total_blks, total_chunks, image_checksum;
};
struct chunk_header {
  uint16_t chunk_type, reserved;
  uint32_t chunk_sz;   /* in blocks */
  uint32_t total_sz;   /* bytes including this header */
};
#pragma pack(pop)

static int write_all(int fd, const void *buf, size_t n) {
  const char *p = buf;
  while (n) {
    ssize_t w = write(fd, p, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    p += w;
    n -= (size_t)w;
  }
  return 0;
}

static int read_all(int fd, void *buf, size_t n) {
  char *p = buf;
  while (n) {
    ssize_t r = read(fd, p, n);
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (r == 0) return -1;
    p += r;
    n -= (size_t)r;
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s sparse raw\n", argv[0]);
    return 1;
  }
  int in = open(argv[1], O_RDONLY);
  if (in < 0) {
    perror(argv[1]);
    return 1;
  }
  int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out < 0) {
    perror(argv[2]);
    return 1;
  }

  struct sparse_header sh;
  if (read_all(in, &sh, sizeof(sh)) < 0 || sh.magic != SPARSE_MAGIC) {
    fprintf(stderr, "bad sparse magic\n");
    return 1;
  }
  if (sh.file_hdr_sz > sizeof(sh))
    lseek(in, sh.file_hdr_sz - sizeof(sh), SEEK_CUR);

  uint32_t i;
  for (i = 0; i < sh.total_chunks; i++) {
    struct chunk_header ch;
    if (read_all(in, &ch, sizeof(ch)) < 0) {
      fprintf(stderr, "chunk hdr read\n");
      return 1;
    }
    if (sh.chunk_hdr_sz > sizeof(ch))
      lseek(in, sh.chunk_hdr_sz - sizeof(ch), SEEK_CUR);

    uint64_t data_len = (uint64_t)ch.chunk_sz * sh.blk_sz;
    if (ch.chunk_type == CHUNK_RAW) {
      char *buf = malloc(1 << 20);
      if (!buf) return 1;
      uint64_t left = data_len;
      while (left) {
        size_t n = left > (1 << 20) ? (1 << 20) : (size_t)left;
        if (read_all(in, buf, n) < 0 || write_all(out, buf, n) < 0) {
          free(buf);
          return 1;
        }
        left -= n;
      }
      free(buf);
    } else if (ch.chunk_type == CHUNK_FILL) {
      uint32_t fill;
      if (read_all(in, &fill, 4) < 0) return 1;
      char block[4096];
      size_t bs = sizeof(block);
      size_t j;
      for (j = 0; j < bs; j += 4) memcpy(block + j, &fill, 4);
      uint64_t left = data_len;
      while (left) {
        size_t n = left > bs ? bs : (size_t)left;
        if (write_all(out, block, n) < 0) return 1;
        left -= n;
      }
    } else if (ch.chunk_type == CHUNK_DONT_CARE) {
      char z[4096];
      memset(z, 0, sizeof(z));
      uint64_t left = data_len;
      while (left) {
        size_t n = left > sizeof(z) ? sizeof(z) : (size_t)left;
        if (write_all(out, z, n) < 0) return 1;
        left -= n;
      }
    } else if (ch.chunk_type == CHUNK_CRC32) {
      uint32_t crc;
      if (read_all(in, &crc, 4) < 0) return 1;
    } else {
      fprintf(stderr, "unknown chunk %x\n", ch.chunk_type);
      return 1;
    }
  }
  close(in);
  close(out);
  return 0;
}
