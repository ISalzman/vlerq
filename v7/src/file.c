/*  Load views from a Metakit-format memory map.
    $Id$
    This file is part of Vlerq, see src/vlerq.h for full copyright notice. */

/* forward */
static vqView MapSubview (vqMap map, intptr_t offset, vqView meta);

typedef struct vqDataVec_s *vqDataVec;
    
struct vqDataVec_s {
    const char *data;
    vqView meta;
    vqDataVec sizes;
    vqMap map;
    vqInfo info;
};

#define dvData(v)   vHead(v,data)
#define dvMeta(v)   vHead(v,meta)
#define dvSizes(v)  vHead(v,sizes)
#define dvMap(v)    vHead(v,map)

#if 0
static void BytesCleaner (vqVec map) {
    /* ObjRelease(map[2].p); FIXME: need to release object somehow */
}

static vqDispatch vbytes = { "bytes", 1, 0, BytesCleaner };

static vqVec OpenMappedBytes (const void *data, int length, Object_p ref) {
    vqVec map = alloc_vec(&vbytes, 3 * sizeof(vqCell));
    map[0].s = map[1].s = (void*) data;
    map[0].x.y.i = map[1].x.y.i = length;
    map[2].p = ref; /* ObjRetain(ref); */
    return map;
}
#endif

static vqType Rgetter_i0 (int row, vqCell *cp) {
    VQ_UNUSED(row);
    cp->i = 0;
    return VQ_int;
}
static vqType Rgetter_i1 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv);
    cp->i = (ptr[row>>3] >> (row&7)) & 1;
    return VQ_int;
}
static vqType Rgetter_i2 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv);
    cp->i = (ptr[row>>2] >> 2*(row&3)) & 3;
    return VQ_int;
}
static vqType Rgetter_i4 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv);
    cp->i = (ptr[row>>1] >> 4*(row&1)) & 15;
    return VQ_int;
}
static vqType Rgetter_i8 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv);
    cp->i = (int8_t) ptr[row];
    return VQ_int;
}

#ifdef VQ_MUSTALIGN
static vqType Rgetter_i16 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const uint8_t *ptr = (const uint8_t*) dvData(dv) + row * 2;
#ifdef VQ_BIGENDIAN
    cp->i = (((int8_t) ptr[0]) << 8) | ptr[1];
#else
    cp->i = (((int8_t) ptr[1]) << 8) | ptr[0];
#endif
    return VQ_int;
}
static vqType Rgetter_i32 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv) + row * 4;
    int i;
    for (i = 0; i < 4; ++i)
        cp->b[i] = ptr[i];
    return VQ_int;
}
static vqType Rgetter_i64 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv) + row * 8;
    int i;
    for (i = 0; i < 8; ++i)
        cp->b[i] = ptr[i];
    return VQ_wide;
}
static vqType Rgetter_f32 (int row, vqCell *cp) {
    Rgetter_i32(row, cp);
    return VQ_float;
}
static vqType Rgetter_f64 (int row, vqCell *cp) {
    Rgetter_i64(row, cp);
    return VQ_double;
}
#else
static vqType Rgetter_i16 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv);
    cp->i = ((short*) ptr)[row];
    return VQ_int;
}
static vqType Rgetter_i32 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv);
    cp->i = ((const int*) ptr)[row];
    return VQ_int;
}
static vqType Rgetter_i64 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv);
    cp->l = ((const int64_t*) ptr)[row];
    return VQ_long;
}
static vqType Rgetter_f32 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv);
    cp->f = ((const float*) ptr)[row];
    return VQ_float;
}
static vqType Rgetter_f64 (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv);
    cp->d = ((const double*) ptr)[row];
    return VQ_double;
}
#endif

static vqType Rgetter_i16r (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const uint8_t *ptr = (const uint8_t*) dvData(dv) + row * 2;
#ifdef VQ_BIGENDIAN
    cp->i = (((int8_t) ptr[1]) << 8) | ptr[0];
#else
    cp->i = (((int8_t) ptr[0]) << 8) | ptr[1];
#endif
    return VQ_int;
}
static vqType Rgetter_i32r (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv) + row * 4;
    int i;
    for (i = 0; i < 4; ++i)
        ((char*) cp)[i] = ptr[3-i];
    return VQ_int;
}
static vqType Rgetter_i64r (int row, vqCell *cp) {
    vqDataVec dv = cp->p;
    const char *ptr = (const char*) dvData(dv) + row * 8;
    int i;
    for (i = 0; i < 8; ++i)
        ((char*) cp)[i] = ptr[7-i];
    return VQ_long;
}
static vqType Rgetter_f32r (int row, vqCell *cp) {
    Rgetter_i32r(row, cp);
    return VQ_float;
}
static vqType Rgetter_f64r (int row, vqCell *cp) {
    Rgetter_i64r(row, cp);
    return VQ_double;
}

static void Rcleaner (void *p) {
    vqDataVec v = p;
    vq_decref(dvMap(v));
}

static vqDispatch vget_i0   = {
    "get_i0"  , sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_i0  
};
static vqDispatch vget_i1   = {
    "get_i1"  , sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_i1  
};
static vqDispatch vget_i2   = {
    "get_i2"  , sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_i2  
};
static vqDispatch vget_i4   = {
    "get_i4"  , sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_i4  
};
static vqDispatch vget_i8   = {
    "get_i8"  , sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_i8  
};
static vqDispatch vget_i16  = {
    "get_i16" , sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_i16 
};
static vqDispatch vget_i32  = {
    "get_i32" , sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_i32 
};
static vqDispatch vget_i64  = {
    "get_i64" , sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_i64 
};
static vqDispatch vget_i16r = {
    "get_i16r", sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_i16r
};
static vqDispatch vget_i32r = {
    "get_i32r", sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_i32r
};
static vqDispatch vget_i64r = {
    "get_i64r", sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_i64r
};
static vqDispatch vget_f32  = {
    "get_f32" , sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_f32 
};
static vqDispatch vget_f64  = {
    "get_f64" , sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_f64 
};
static vqDispatch vget_f32r = {
    "get_f32r", sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_f32r
};
static vqDispatch vget_f64r = {
    "get_f64r", sizeof(struct vqDataVec_s), 0, Rcleaner, Rgetter_f64r
};

static vqDispatch* PickIntGetter (int bits) {
    switch (bits) {
        case 0:     return &vget_i0;
        case 1:     return &vget_i1;
        case 2:     return &vget_i2;
        case 4:     return &vget_i4;
        case 8:     return &vget_i8;
        case 16:    return &vget_i16;
        case 32:    return &vget_i32;
        case 64:    return &vget_i64;
    }
    assert(0);
    return 0;
}

static vqDispatch* FixedGetter (int bytes, int rows, int real, int flip) {
    static char widths[8][7] = {
        {0,-1,-1,-1,-1,-1,-1},
        {0, 8,16, 1,32, 2, 4},
        {0, 4, 8, 1,16, 2,-1},
        {0, 2, 4, 8, 1,-1,16},
        {0, 2, 4,-1, 8, 1,-1},
        {0, 1, 2, 4,-1, 8,-1},
        {0, 1, 2, 4,-1,-1, 8},
        {0, 1, 2,-1, 4,-1,-1},
    };
    int bits = rows < 8 && bytes < 7 ? widths[rows][bytes] : (bytes<<3) / rows;
    switch (bits) {
        case 16:    return flip ? &vget_i16r : &vget_i16;
        case 32:    return real ? flip ? &vget_f32r : &vget_f32
                                : flip ? &vget_i32r : &vget_i32;
        case 64:    return real ? flip ? &vget_f64r : &vget_f64
                                : flip ? &vget_i64r : &vget_i64;
    }
    return PickIntGetter(bits);
}

/* ----------------------------------------------- METAKIT UTILITY CODE ----- */

static int IsReversedEndian (vqMap map) {
#ifdef VQ_BIGENDIAN
    return *map->data == 'J';
#else
    return *map->data == 'L';
#endif
}

static intptr_t GetVarInt (const char **nextp) {
    int8_t b;
    intptr_t v = 0;
    do {
        b = *(*nextp)++;
        v = (v << 7) + b;
    } while (b >= 0);
    return v + 128;
}

static intptr_t GetVarPair (const char **nextp) {
    intptr_t n = GetVarInt(nextp);
    if (n > 0 && GetVarInt(nextp) == 0)
        *nextp += n;
    return n;
}

/* ----------------------------------------------- MAPPED TABLE COLUMNS ----- */

static void MappedViewCleaner (void *p) {
    vqDataVec v = p;
    const intptr_t *offsets = (void*) v;
    int i, count = vSize(v);
    for (i = 0; i < count; ++i)
        if ((offsets[i] & 1) == 0)
            vq_decref((void*) offsets[i]);
    vq_decref(dvMeta(v));
    vq_decref(dvMap(v));
}

static vqType MappedViewGetter (int row, vqCell *cp) {
    vqDataVec v = cp->p;
    intptr_t *offsets = (void*) v;
    
    /* odd means it's a file offset, else it's a cached view pointer */
    if (offsets[row] & 1) {
        intptr_t o = (uintptr_t) offsets[row] >> 1;
        offsets[row] = (intptr_t) vq_incref(MapSubview(dvMap(v), o, dvMeta(v)));
        assert((offsets[row] & 1) == 0);
    }
    
    cp->v = (void*) offsets[row];
    return VQ_view;
}

static vqDispatch mvtab = {
    "mappedview", sizeof(struct vqDataVec_s), 0,
    MappedViewCleaner, MappedViewGetter
};

static vqDataVec MappedvwCol (vqMap map, int rows, const char **nextp, vqView meta) {
    int r, c, cols, subcols;
    intptr_t colsize, colpos, *offsets;
    const char *next;
    vqDataVec result;
    
    result = alloc_vec(&mvtab, rows * sizeof(vqView*));
    offsets = (void*) result;
    
    cols = vwRows(meta);
    
    colsize = GetVarInt(nextp);
    colpos = colsize > 0 ? GetVarInt(nextp) : 0;
    next = map->data + colpos;
    
    for (r = 0; r < rows; ++r) {
        offsets[r] = 2 * (next - map->data) | 1; /* see MappedViewGetter */
        GetVarInt(&next);
        if (cols == 0) {
            intptr_t desclen = GetVarInt(&next);
            meta = desc2meta(map->state, next, desclen);
            next += desclen;
        }
        if (GetVarInt(&next) > 0) {
            subcols = vwRows(meta);
            for (c = 0; c < subcols; ++c)
                switch (vq_getInt(meta, c, 1, VQ_nil) & VQ_TYPEMASK) {
                    case VQ_bytes: case VQ_string:
                        if (GetVarPair(&next))
                            GetVarPair(&next);
                }
                GetVarPair(&next);
        }
    }
    
    vSize(result) = rows;
    dvMap(result) = vq_incref(map);
    dvMeta(result) = vq_incref(cols > 0 ? meta : empty_meta(map->state));    
    return result;
}

/* ------------------------------------ MAPPED FIXED-SIZE ENTRY COLUMNS ----- */

static vqDataVec MappedFixedCol (vqMap map, int rows, const char **nextp, int real) {
    intptr_t bytes = GetVarInt(nextp);
    int colpos = bytes > 0 ? GetVarInt(nextp) : 0;
    vqDispatch *vt = FixedGetter(bytes, rows, real, IsReversedEndian(map));
    vqDataVec result = alloc_vec(vt, 0);    
    vSize(result) = rows;
    dvData(result) = (void*) (map->data + colpos);
    dvMap(result) = vq_incref(map);
    return result;
}

/* ---------------------------------------------- MAPPED STRING COLUMNS ----- */

static void MappedStringCleaner (void *p) {
    vqDataVec v = p;
    vq_decref(dvSizes(v));
    vq_decref(dvMap(v));
}

static vqType MappedStringGetter (int row, vqCell *cp) {
    vqDataVec v = cp->p;
    const intptr_t *offsets = (void*) v;
    const char *data = dvMap(v)->data;

    if (offsets[row] == 0)
        cp->s = "";
    else if (offsets[row] > 0)
        cp->s = data + offsets[row];
    else {
        const char *next = data - offsets[row];
        GetVarInt(&next);
        cp->s = data + GetVarInt(&next);
    }

    return VQ_string;
}

static vqDispatch mstab = {
    "mappedstring", sizeof(struct vqDataVec_s), 0,
    MappedStringCleaner, MappedStringGetter
};

static vqType MappedBytesGetter (int row, vqCell *cp) {
    vqDataVec v = cp->p;
    const intptr_t *offsets = (void*) v;
    const char *data = dvMap(v)->data;
    
    if (offsets[row] == 0) {
        cp->x.y.i = 0;
        cp->p = 0;
    } else if (offsets[row] > 0) {
        cp->p = dvSizes(v); /* reuse cell to access sizes */
        getcell(row, cp);
        cp->x.y.i = cp->i;
        cp->p = (char*) data + offsets[row];
    } else {
        const char *next = data - offsets[row];
        cp->x.y.i = GetVarInt(&next);
        cp->p = (char*) data + GetVarInt(&next);
    }

    return VQ_bytes;
}

static vqDispatch mbtab = {
    "mappedstring", sizeof(struct vqDataVec_s), 0,
    MappedStringCleaner, MappedBytesGetter
};

static vqDataVec MappedStringCol (vqMap map, int rows, const char **nextp, int istext) {
    int r;
    intptr_t colsize, colpos, *offsets;
    const char *next, *limit;
    vqDataVec result, sizes;
    vqCell cell;

    result = alloc_vec(istext ? &mstab : &mbtab, rows * sizeof(void*));
    offsets = (void*) result;

    colsize = GetVarInt(nextp);
    colpos = colsize > 0 ? GetVarInt(nextp) : 0;

    if (colsize > 0) {
        sizes = MappedFixedCol(map, rows, nextp, 0);
        for (r = 0; r < rows; ++r) {
            cell.p = sizes;
            getcell(r, &cell);
            if (cell.i > 0) {
                offsets[r] = colpos;
                colpos += cell.i;
            }
        }
    } else
        sizes = alloc_vec(FixedGetter(0, rows, 0, 0), 0);
        
    vq_incref(sizes);
    
    colsize = GetVarInt(nextp);
    next = map->data + (colsize > 0 ? GetVarInt(nextp) : 0);
    limit = next + colsize;
    
    /* negated offsets point to the size/pos pair in the map */
    for (r = 0; next < limit; ++r) {
        r += (int) GetVarInt(&next);
        offsets[r] = map->data - next;
        assert(offsets[r] < 0);
        GetVarPair(&next);
    }

    vSize(result) = rows;
    dvMap(result) = vq_incref(map);
    
    if (istext)
        vq_decref(sizes);
    else
        dvSizes(result) = sizes;

    return result;
}

/* ----------------------------------------------------- TREE TRAVERSAL ----- */

static vqView MapCols (vqMap map, const char **nextp, vqView meta) {
    int c, cols, r, rows;
    vqView result, sub;
    vqDataVec vec;
    
    rows = GetVarInt(nextp);
    cols = vwRows(meta);
    
    result = vq_new(meta, 0);
    vwRows(result) = rows;
    
    if (rows > 0)
        for (c = 0; c < cols; ++c) {
            r = rows;
            switch (vq_getInt(meta, c, 1, VQ_nil) & VQ_TYPEMASK) {
                case VQ_int:
                case VQ_long:
                    vec = MappedFixedCol(map, r, nextp, 0); 
                    break;
                case VQ_float:
                case VQ_double:
                    vec = MappedFixedCol(map, r, nextp, 1);
                    break;
                case VQ_string:
                    vec = MappedStringCol(map, r, nextp, 1); 
                    break;
                case VQ_bytes:
                    vec = MappedStringCol(map, r, nextp, 0);
                    break;
                case VQ_view:
                    sub = vq_getView(meta, c, 2, 0);
                    vec = MappedvwCol(map, r, nextp, sub); 
                    break;
                default:
                    assert(0);
                    return result;
            }
            vwCol(result,c).v = vq_incref(vec);
        }

    return result;
}

static vqView MapSubview (vqMap map, intptr_t offset, vqView meta) {
    const char *next = map->data + offset;
    GetVarInt(&next);
    
    if (vwRows(meta) == 0) {
        intptr_t desclen = GetVarInt(&next);
        meta = desc2meta(map->state, next, desclen);
        next += desclen;
    }

    return MapCols(map, &next, meta);
}

static int BigEndianInt32 (const char *p) {
    const uint8_t *up = (const uint8_t*) p;
    return (p[0] << 24) | (up[1] << 16) | (up[2] << 8) | up[3];
}

vqView MapToView (vqMap map) {
    int i, t[4];
    intptr_t datalen, rootoff;
    
    if (map->size <= 24 || *(map->data + map->size - 16) != '\x80')
        return 0;
        
    for (i = 0; i < 4; ++i)
        t[i] = BigEndianInt32(map->data + map->size - 16 + i * 4);
        
    datalen = t[1] + 16;
    rootoff = t[3];

    if (rootoff < 0) {
        const intptr_t mask = 0x7FFFFFFF; 
        datalen = (datalen & mask) + ((intptr_t) ((t[0] & 0x7FF) << 16) << 15);
        rootoff = (rootoff & mask) + (datalen & ~mask);
        /* FIXME: rollover at 2 Gb, prob needs: if (rootoff > datalen) ... */
    }
    
    map = new_map(map->state, 0, map, map->data, datalen);
    return MapSubview(map, rootoff, empty_meta(map->state));
}
