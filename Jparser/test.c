#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>

#define __FORCE_INLINE__ __attribute__((always_inline))
#define __NONNULL__ __attribute__((nonnull))
#define CHOOSE_EXPR(cExpr, tVal, fVal)                \
    __builtin_choose_expr(cExpr, tVal, fVal)
/* fprintf(stderr, "[vector] " "An attempt to add '%p' at index %lu to an object of size %lu failed\n", vd, (long)index, (long)sz);*/
#define throwError(...) (void *)0
/*
 * vector
 */
typedef uint32_t VEC_szType;
typedef uint8_t word8;

#define VEC_DATA_BLOCK_SZ (sizeof (void *))
#define VEC_META_DATA_SZ(_pre_sz) ((_pre_sz & 0x0f) + 1) /*vector_byte_size and 1 byte for other meta-info */
#define VEC_LEAST_SZ 1
#define VEC_ALWYS_PREALLOC 0x40
#define VEC_ALLOC_SZ 1
#define VEC_APPEND 0xde
#define VEC_VECTOR 0x80
#define VEC_ARRAY 0
#define VEC_SAFE_INDEX_CHECK 1
#define VEC_ALW_WARNING 1
#define VEC_EROUT_OF_BOUND 0x3b

/* remove the type alignment of bytes so that each block can be addressed like a byte array */
#define VEC_ACCESS(_addr) ((word8 *)(void *)(_addr))

#define VEC_SZ_INCR(vec, fl)			\
    switch ((fl) & 0x0f) {					\
    case 0x01: *(volatile word8 *)(vec) += 1; break;		\
    case 0x02: *(volatile uint16_t *)(vec) += 1; break;		\
    case 0x03: *(volatile uint32_t *)(vec) += 1; break;		\
    case 0x04: *(volatile uint64_t *)(vec) += 1; break;		\
    } (void)0

#define VEC_SZEOF(_SZ)				\
    ((_SZ) > UINT8_MAX ?			\
     (_SZ) > UINT16_MAX ?			\
     (_SZ) > UINT32_MAX ?			\
     (_SZ) > UINT64_MAX ?			\
     0x05 : 0x04 : 0x03 : 0x02 : 0x01		\
     )
#define VEC_BLOCK_START(vec, fl)\
    (VEC_ACCESS(vec) - (((fl) & 0x0f) + 1))

#define VEC_MOVTO_DATA_START(vec, fl)				\
    ((vec) = (void *)(VEC_ACCESS(vec) + VEC_META_DATA_SZ(fl)))

static __inline__ __FORCE_INLINE__ void *VEC_getSize(void *vec, void *to) {
    register word8 *s, *v, i;

    v = vec, s = to;

    i = *--v & 0x0f; /* n bytes of vec size */
    v -= i; /* mov v to block start */

    while ( i-- ) {
	*s++ = *v++;
    }
    return to;
}

/*
 * vector_create
 */
static void **VEC_create(size_t vecSize) {
    void *vec;
    word8 mSz;

    if ( !vecSize )
	vecSize = VEC_LEAST_SZ;
    /* meta-data size */
    mSz = VEC_META_DATA_SZ(VEC_SZEOF(vecSize));
 
    if (! (vec = malloc((VEC_DATA_BLOCK_SZ * vecSize) + mSz)))
	return NULL;
    /* initialize size to zero */
    memset(vec, 0, --mSz);

    /* update meta-data: prealloc | type | n bytes allocated for sz ([1100 0001] for size == 1) */
    VEC_ACCESS(vec)[mSz] = ((word8)(vecSize > VEC_LEAST_SZ) << 6) | VEC_VECTOR | mSz;
    /* mov ahead meta-data block (main) */
    VEC_MOVTO_DATA_START(vec, mSz);
/* initialize the first block (main) to 0 [vector is empty] */
    *(void **)vec = 0;

    return vec;
}

/*
 * vector_append: add to last; realloc/copy vector if neccessary
 */
static __inline__  __NONNULL__ void **VEC_append(void ***vec, void *new, VEC_szType sz, word8 meta) {
    void *v0;
    register word8 sb, pb;
    register uintmax_t memtb;

    sb = false; /* True if size reqires an additional block */
    pb = meta & 0x0f; /* n bytes of current size */;

    /* shifting pb bytes should result to 0 if (sz + 1) <= the maximum number pb bytes can represent */
    if ((sz + 1) >> (4ul << pb)) {
	(VEC_ACCESS(*vec) - 1)[0] = (meta & ~pb) | (pb + 1);
	sb = true;
    }
       /* resize vector || allocate an entire new block */
    memtb = (VEC_DATA_BLOCK_SZ * (sz + VEC_ALLOC_SZ)) + VEC_META_DATA_SZ(meta);

    v0 = *vec ? sb == false ? realloc(VEC_BLOCK_START(*vec, meta), memtb) : malloc(memtb + 1) : NULL;

    if (v0 == NULL)
	return (void **)NULL;

    if (sb) {
	/* We are here because we want to pad a new block for size */
	memcpy(v0, &sz, pb);
	(VEC_ACCESS(v0) + pb)[0] = meta;

	/* (local realloc)
	 * This is a trade-off of performance and may be changed in future review.
	 */ 
	memcpy(v0 + meta + 1, *vec, sz * VEC_DATA_BLOCK_SZ);
	free(VEC_BLOCK_START(*vec, meta));
    }
    /* ++sz */
    VEC_SZ_INCR(v0, meta);
    
    *vec = (void *)(VEC_ACCESS(v0) + pb + 1);
    (*vec)[sz] = new; /* implicit [sz - 1] */

    return *vec;
}

/*
 * vec_expand: add new data to vector
 */
static __inline__  __NONNULL__ void **VEC_expand(void ***vec, void *vd, size_t index, size_t vflag) {
    VEC_szType sz, fl;

    (sz = 0) || VEC_getSize(*vec, &sz);

    fl = (VEC_ACCESS(*vec) - 1)[0];

    if (! (*vec)[0] ) {
#if VEC_SAFE_INDEX_CHECK || VEC_ALW_WARNING
	if ((index != 0) || (vflag != VEC_APPEND)) {
	    /* throw out-of-bound error */
	    goto err_outOfBound;
	}
#endif
	/* ignore index || index is 0 */
	(*vec)[index] = vd;
	/* ++sz */
	VEC_SZ_INCR(VEC_BLOCK_START(*vec, fl), fl);
    }
    else if (index < sz) {
	(*vec)[index] = vd;
    }
    else if (! ((vflag & VEC_APPEND) && VEC_append(vec, vd, sz, fl))) {	
	/* out of bound // insufficient memory */
    err_outOfBound:
#if VEC_ALW_WARNING
	vflag & VEC_APPEND && throwError(EROUT_OF_BOUND, vec, vd, index + 1, sz);
#endif
	return NULL;
    }
    return *vec;
}

static  __NONNULL__ void *VEC_add(void ***vec, void *vd, size_t bytesz, size_t sz, size_t index, word8 type) {
    void *v0 /* ptr to memory block */, **v00 /* ptr to vector */;
    word8 meta;
    size_t nalloc;

    v00 = (void *)1; /* prevent unsed v00 folding to 0 */

    meta = type | VEC_SZEOF(sz);
    nalloc = bytesz * sz; /* sizeof(vd) * sz */

    if (*vec == NULL || nalloc < 1
	|| /* block alloc  */ !(v0 = malloc(nalloc + VEC_META_DATA_SZ(meta)))) {
	return NULL;
    }
    memcpy(v0, &sz, meta & 0x0f);
    /* */
    VEC_MOVTO_DATA_START(v0, meta);
    (VEC_ACCESS(v0) - 1)[0] = meta;
    /* copy data to memory including its meta-data */
    memcpy(v0, vd, nalloc);

    /* if the vec_vector type is specified, create a new vector */
    if ((type & VEC_VECTOR) && (v00 = VEC_create(VEC_LEAST_SZ))) {
	/* initialize its first member with the data */
	*v00 = v0;
	/* reuse v0 to retain a generic referencing for both non-vector or vector using a void ptr */
	v0 = v00;
    }
    return v00 && VEC_expand(vec, v0, index, VEC_APPEND) ? v0 : NULL;
}
static __NONNULL__ __inline__ __attribute__((always_inline, pure)) void *VEC_getVectorItem(void **vec, ssize_t index) {
    size_t sz;

    (sz = 0) || VEC_getSize(*vec, &sz);
    index = sz + ( index < 0 ? index : 0 );

    if (index > sz || index < 0) {
#if VEC_ALW_WARNING
	throwError(ERGOUT_OUT_OF_BOUND, *vec, sz, index);
#endif
	return NULL;
    }
    return vec[index];
}
static __NONNULL__ __inline__ __FORCE_INLINE__ void *VEC_getVectorItemAt(void ***vec, ssize_t index, ssize_t at) {
    void *itemAt;

    itemAt = VEC_getVectorItem(*vec, at);
    return itemAt ? VEC_getVectorItem(&itemAt, index) : NULL;
}
static __inline__ __FORCE_INLINE__  __NONNULL__ uint8_t VEC_getType(void *vec) {
    /* v & VEC_VECTOR == 0 (array) */
    return ( (VEC_ACCESS(vec) - 1)[0] & VEC_VECTOR );
}
static __NONNULL__ void *VEC_remove(void ***vec, ssize_t index) {
}

#define VEC_INCR_STACK_CNTER(stkc)(++(stkc))
#define VEC_DECR_STACK_CNTER(stkc)(--(stkc))
#define VEC_RECURS_DEPTH 1000
#define STACK_init(...)(void)0
#define pushToStack(...)(void )0
#define popToStack(...)(void )0
#define STACK_INTERNAL_ERROR 124
typedef intmax_t _stframe_t;

static __NONNULL__ void *VEC_internalDelete(void ***vec, uint16_t stackCt){
    uint8_t extMeta;
    size_t sz, luptrk, i;
    void *ptrv, *dynStack;

#if !VEC_OPTMZ_NOSTACK && defined(VEC_STACK_LIM) && ((VEC_STACK_SZ > 0) && (VEC_STACK_SZ < VEC_STACK_MLIMIT)

    void *_locStackvr[VEC_STACK_LIM];
    register _stframe_t _tframe;

    _trackframe = -1;
    
#define push(val)\
    ((_locStackvr)[VEC_INCR_STACK_FRAME(_tframe)] = (val))
#define pop(...)\
    (_rmframe(_locStackvr[_tframe]),\
     _locStackvr[VEC_DECR_STACK_FRAME(_tframe)])
#else
    objStack _locStackvr = STACK_init();

#define push(val)\
    pushToStack(_locStackvr, (val) || throwError(STACK_INTERNAL_ERROR)
#define pop(...)\
    (popFromStack(_locStackvr), getFromStack(_locStackvr))
#endif
    sz = 0;
    luptrk = 1;
    do {
	if (*vec == NULL)
	    return NULL;
	extMeta = (VEC_ACCESS(*vec) - 1)[0];
	if ( !(extMeta & VEC_VECTOR) ) {
	    /* just a vector array */
	    break;
	}
	VEC_getSize(*vec, &sz);
	for (i = 0, ptrv = *vec; i < sz; i++, ++ptrv) {
	    if (VEC_getType(*ptrv) == VEC_VECTOR) {
		push(ptrv);
		ptrv = *ptrv;
		continue;
	    }
	    if (++i == sz) {
		pop(ptrv);

		continue;
	    }
	    VEC_INCR_STACK_CNTER(stackCt);
	    /* call del recursively on vec [i] */
	    VEC_internalDelete(ptrv, stackCt);
	    VEC_DECR_STACK_CNTER(stackCt);
	    continue;
	    }
	    free(VEC_BLOCK_START(ptrv, (VEC_ACCESS(*vec) - 1)[0]));
	}
    } while (--luptrk);

    free(VEC_BLOCK_START(*vec, extMeta));
}

/*
  ptrv = vec[i]
  if (ptrv =  )
  if ptrv == 
  in using local stack, we need to remember our current index
 */

static __NONNULL__ void *VEC_delete(void ***vec) {
    uint16_t stackCounter;
    stackCounter = 0;
    return VEC_internalDelete(vec, stackCounter);
}
int main(void) {
    int num[1024] = {0};
    int p = 0;
    VEC_szType x;
    void **vec = VEC_create(1);
    void **new;
    int data[][6] = {
	{0, 2, 4, 6, 8, 10}, {1, 3, 5, 7, 9, 11},
	{2, 3, 5, 7, 11, 13}, {4, 16, 32, 64, 128},
	{3, 6, 9, 12, 15, 18}, {7, 14, 21, 28, 35, 42},
	{10, 20, 30, 40, 50, 60}, {15, 45, 60, 75, 90, 105},
	{20, 40, 60, 80, 100, 120}, {50, 100, 150, 200, 250, 300},
	{0, 0, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1}
    };
    VEC_add(&vec, data[0], sizeof(int), 6, 0, VEC_ARRAY);
    VEC_add(&vec, data[1], sizeof(int), 6, 1, VEC_ARRAY);
    new = VEC_add(&vec, data[2], sizeof(int), sizeof data[2], 2, VEC_VECTOR);
    if (new != 0)
	VEC_add(&new, data[3], sizeof(int), sizeof data[3], 1, VEC_ARRAY);

    new = VEC_add(&new, data[4], sizeof(int), sizeof data[4], 3, VEC_VECTOR);
    if (new != 0)
	VEC_add(&new, data[5], sizeof(int), sizeof data[5], 1, VEC_ARRAY);
    
    p =  (VEC_ACCESS(vec) - 1)[0] & 0x0f;
    x = 0;

    memcpy(&x, VEC_BLOCK_START(vec, p), p);
    printf("%u\n", x);

    p = 3;
    VEC_SZ_INCR(&p, sizeof p);
    VEC_delete(&vec);
    printf("%d\n", new == 0);
}
