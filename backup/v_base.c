#define VEC_INTERNAL_IMPLEMENTATION
#include "v_base.h"

/* Allow size-bound check on request */
#define VEC_SAFE_REQUEST 1

/* enable debuging by default */
#if !defined(VEC_DEBUG)
#define VEC_DEBUG 1
#endif

/* Meta-data */
typedef struct {
  uint32_t cap;
#if !defined(VEC_MAX_RM)
#define VEC_MAX_RM 0xff
#endif
  size_t rmbf[VEC_MAX_RM];
  uint8_t rmct;
} vecMetaDataHeader;

/*
  segmentation block-list Header
*/
typedef struct {
  uint8_t blockFill; /* size of block (maximum: 256) */
  void *nextBlock;
} segmentBlock;

enum {
      VEC_MIN_SIZE         = 0x100,
      VEC_ALLOC_SIZE       = 0x01,
      VEC_VECTOR           = 0x80,
      VEC_SEGTBLK          = 0x01,
      VEC_MALLOCD_ALL      = 0x04,
      VEC_MEMFILL          = 0x01,
      VEC_REM_OPTMZ        = USHRT_MAX
};

/**
 * (gblobal)
 *
 * @1 flags (fits in a single byte)
 * @2 Meta-data size
 * @3 Size of allocation block for entries
 * @3 Size of segment
 */
const uint8_t  vecDefFlag             = 1;
const uint16_t  vecGblMetaDataSize     = sizeof (vecMetaDataHeader) + vecDefFlag;
const uint16_t  vecGblDataBlockSize    = sizeof (vec_t);
const uint16_t vecGblSegmentBlockCap  = 0x100;
const uint16_t vecGlbSegmentAllocSize = vecGblDataBlockSize * vecGblSegmentBlockCap;
#define        vecGblInverse256         0.00390625

/**
 * (macros)
 *
 * @VEC_metaData: Get/set meta-data in header
 * @VEC_peekBlockStart: Temporarily view the starting block of the vector
 * @VEC_size: Request or update the size of the container
 * @VEC_moveToMainBlock: Move pointer to the data section
 * @VEC_moveToBlockStart: Move the pointer to the start of the contaner’s block
 * @VEC_reinterpret: Reinterpret as pointer to single byte memory
 * @VEC_nextblock & @VEC_prevblock: Increment/decrement memory pointer
 */
#define VEC_metaData(vec, ...)    ((VEC_reinterpret(vec) - vecDefFlag)[0])
#define VEC_peekBlockStart(vec)   (void *)(VEC_reinterpret(vec) - vecGblMetaDataSize)
#define VEC_size(vec)             ((vecMetaDataHeader *)VEC_peekBlockStart(vec))->cap
#define VEC_moveToMainBlock(vec)  ((vec) = (void * )(VEC_reinterpret(vec) + vecGblMetaDataSize))
#define VEC_moveToBlockStart(vec) ((vec) = VEC_peekBlockStart(vec))
#define VEC_reinterpret(_addr)    ((uint8_t *)(void *)(_addr))
#define VEC_rmbuffer(new)         ((vecMetaDataHeader *)VEC_peekBlockStart(vec))->rmbf
#define VEC_rmCounter(vec)        ((vecMetaDataHeader *)VEC_peekBlockStart(vec))->rmct
#define VEC_nextblock(block)      ((block)++)
#define VEC_prevblock(block)      ((block)--)

/**
 * if VEC_TRACE_DEL is defined, caching is enabled. Memory of deleted contents of are cached rather than being freed.
 * This minimizes the frequent call to a memory allocator, at a cost of speed of insertion, since the list of deleted
 * content may have to be scanned for each insertion request.
 */
#ifdef VEC_ENABLE_CACHE

/**
 * TODO: cache only memory, ignore location of content
 */
  __STATIC_FORCE_INLINE_F void VEC_cache(const void *vec, size_t at) {
    vec_t slot, cacheLoc;
    vecMetaDataHeader *cacheInfo __MAY_ALIAS__;

    cacheInfo = (vecMetaDataHeader *)VEC_peekBlockStart(vec);
    cacheLoc = &(cacheInfo->vcache);

    /* TODO: size-bound check (use request) */
    slot = (vec_t)vec + at;
    /* cache only slot, discard its content */
    (*slot != NULL) && VEC_free(*slot);
    *slot = *cacheLoc, *cacheLoc = slot;


    cacheInfo->vcacheSize += 1;
    cacheInfo->vcapSize -= 1;
  }

__STATIC_FORCE_INLINE_F void VEC_rcache(void *vec, size_t at) {
  vec_t cacheLoc, cachePrevLoc;

  /* retrieve cache list */
  cacheLoc = ((vecMetaDataHeader *)VEC_peekBlockStart(vec))->vcache;
  cachePrevLoc = NULL;

  /*
   * if the difference between the initial memory address of the container and that
   * of the cache pointer equals that of the requested i, we had cached the
   * memory on a previous operation
   */
  while (cacheLoc && (ptrdiff_t)((cacheLoc - (vec_t)vec) != at))
    cachePrevLoc = cacheLoc, cacheLoc = *cacheLoc;

  /*
    cacheloc will never be null assuming no cache miss. remove the found memory from the cache list
  */
  if (cacheLoc) {
    cachePrevLoc && ( *cachePrevLoc = cacheLoc );
    ((vecMetaDataHeader *)VEC_peekBlockStart(vec))->vcacheSize -= 1;
  }
}
#endif /**/

/**
 * VEC_SEGMENT
 *
 * segments vector into blocks
 */
static __NONNULL__ vec_t VEC_segment(vec_t vec, size_t size, uint8_t action) {
  segmentBlock **block;
  uint32_t segment;

  /* segment size (each block having atleast 256bytes size) */
  segment = (segment = (size >> 8)) + !!(size - (segment << 8));

  /* allocate memory of size equal to segment to vwctor */
  mvpgAlloc(&vec, safeMulAdd(vecGblDataBlockSize, segment, vecGblMetaDataSize), 0);

  block = (void *)VEC_moveToMainBlock(vec);

  VEC_size(vec) = segment;

  /* enable/disable action (memfill) if segment size is lesser/greater than the number of reasonable malloc call per function. 0 >= n <= 2^8 is choosen as the limit */
  action = action >> (segment > (2 << 8));
  VEC_metaData(vec) = VEC_SEGTBLK & (!!action & VEC_MALLOCD_ALL);

  do {
    mvpgAlloc(block, vecGlbSegmentAllocSize, 0);
    (*block)->blockFill = 0;           /* property fill */
    (*block) += sizeof (segmentBlock); /* move ahead of header */
  } while (VEC_nextblock(block), (action & VEC_MEMFILL) && --segment );

  return vec;
}

/**
 * VEC_CREATE
 *
 * Constructor
 */
__attribute__ ((warn_unused_result)) vec_t VEC_create(size_t size, const VEC_set config) {
  vec_t vec;
  uint8_t _vecMetaData;

  !size && (size = VEC_MIN_SIZE);

  /* multiblock (table) */
  if (config.type & VEC_SEGTBLK) {
    return VEC_segment(vec, size, config.memfill);
  }
  /* traditional vector with a large, single block */
  mvpgAlloc(&vec, safeMulAdd(vecGblDataBlockSize, size, vecGblMetaDataSize), 0);
  VEC_moveToMainBlock(vec);
  VEC_size(vec) = size;

  return vec;
}

/**
 * VEC_RESIZE
 *
 * resizes vector
 */
__NONNULL__ vec_t VEC_resize(vec_t *vec, ssize_t newSize) {
  void *alloc;
  ssize_t allocSize;

  allocSize = safeMulAdd(vecGblDataBlockSize, newSize, vecGblMetaDataSize);

  VEC_moveToBlockStart(*vec);
  mvpgAlloc(&alloc, allocSize, 0);
  memcpy(alloc, *vec, safeMulAdd(vecGblDataBlockSize, VEC_size(*vec), vecGblMetaDataSize));
  free(*vec);

  *vec = VEC_moveToMainBlock(alloc);

  VEC_size(*vec) = newSize;

  return *vec;
}

/**
 * VEC_add
 */
__NONNULL__ void VEC_add(vec_t *vec, void *new, size_t size, size_t i) {

  assert (*vec);

  (VEC_size(*vec) < i) && VEC_resize(vec, i);
  VEC_findNextNonEmpty(*vec, i)[0] = new;
}

#define peekBlkStart(seg) ((segmentBlock *)(VEC_reinterpret(seg) - sizeof (segmentBlock)))


__NONNULL__ void VEC_sAdd(vec_t *vec, void *new, size_t size, size_t i) {
  size_t row, col;
  segmentBlock *pRow __MB_UNUSED__ ;

  assert(*vec);

  row = (i >> 8) + !!( i & 0xff );                    /* row index */
  col = vecGblSegmentBlockCap * (1 - (row - (vecGblInverse256 * i)));  /* Column index */

  (row > VEC_size(*vec)) && VEC_resize(vec, row);

  /* space may not have been allocated to row */
  (pRow == NULL) && mvpgAlloc(pRow, vecGlbSegmentAllocSize, sizeof (segmentBlock));

  peekBlkStart(pRow)->blockFill += 1;
  memcpy(pRow + col, new, size);

#undef peekBlkStart
}
/**
 * VEC_get
 */
__STATIC_FORCE_INLINE_F __NONNULL__ vec_t VEC_findNextNonEmpty(vec_t vec, size_t frm) {
  register size_t end;

  for (end = VEC_size(vec) - frm; (frm < end) && !vec[frm]; frm++) {
    PASS;
  }
  return vec + frm;
}
__STATIC_FORCE_INLINE_F __NONNULL__ vec_t VEC_get(vec_t vec, ssize_t i) {

  if (i < 0)
    i = -i;
  assert(i < VEC_size(vec));

  return VEC_findNextNonEmpty(vec, i);
}

/**
 * VEC_remove
 */
__NONNULL__ void VEC_remove(vec_t *vec, ssize_t i) {

  if (VEC_size(*vec) > VEC_REM_OPTMZ) {
    ((vec_t)VEC_get(*vec, i))[0] = NULL;
    VEC_rmbuffer(*vec)[VEC_rmCounter(*vec)] = i;
    VEC_rmCounter(*vec) += 1;
    if (VEC_rmCounter(*vec) == VEC_MAX_RM) {
      /* TODO: cleanup */
      VEC_rmCounter(*vec) = 0;
    }
  }
  else {
    size_t mvby = (VEC_size(*vec) - i - 1) * sizeof(vec_t);
    mvby != 1 ? memmove(*vec + i, (*vec + i) + 1, mvby) /* Shift memory to left */
      : ((*vec)[i] = (void *)MEMCHAR); /* Last index: reusable */
  }
}

__STATIC_FORCE_INLINE_F __NONNULL__ uint8_t VEC_getLevel(void *vec) {
  /*
   * Non vector if; v & VEC_VECTOR is 0
   */
  return VEC_metaData(vec) & VEC_VECTOR;
}

/**
 * Get size of vector
 */
__NONNULL__ size_t VEC_getsize(const vec_t vec) {

  return VEC_size(vec);
}

/**
 * VEC_free
 */
__FORCE_INLINE__ inline bool VEC_free(void *vec) {

  free( VEC_moveToBlockStart(vec) );

  return true;
}

/**
 * VEC_shrink
 */
__STATIC_FORCE_INLINE_F void *VEC_shrink(void *ptr) {

  /* NOT IMPLEMENTED */

  return NULL;
}

/**
 *  VEC_delete
 */
__NONNULL__ void *VEC_delete(vec_t *vec) {

  /* NOT IMPLEMENTED */

  return NULL;
}
