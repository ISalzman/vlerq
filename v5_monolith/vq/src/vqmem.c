/* Vlerq memory management */
/* TODO: adjust names to use vq_ prefix */

#include "vqdefs.h"

#include <stdlib.h>
#include <string.h>

Vector AllocVector (Dispatch *vtab, int bytes) {
    Vector result;
    bytes += vtab->prefix * sizeof(vq_Item);
    result = (Vector) memset(malloc(bytes), 0, bytes) + vtab->prefix;
    vType(result) = vtab;
    return result;
}

void FreeVector (Vector v) {
    int prefix = vType(v)->prefix;
#ifndef NDEBUG
    memset(v - prefix, 0xFB, prefix * sizeof *v);
#endif
    free(v - prefix);
}

#pragma mark -

typedef struct Overflow {
    char        b[4096]; /* must be first member */
    Overflow_p  next;
} Overflow;

void InitBuffer (Buffer_p bp) {
    bp->fill.c = bp->buf;
    bp->limit = bp->buf + sizeof bp->buf;
    bp->head = 0;
    bp->ofill = 0;
    bp->saved = 0;
    bp->result = 0;
}

void ReleaseBuffer (Buffer_p bp, int keep) {
    while (bp->head != 0) {
        Overflow_p op = bp->head;
        bp->head = op->next;
        free(op);
    }
    if (!keep && bp->result != 0)
        free(bp->result);
}

void AddToBuffer (Buffer_p bp, const void *data, intptr_t len) {
    intptr_t n;
    while (len > 0) {
        if (bp->fill.c >= bp->limit) {
            if (bp->head == 0 || 
                    bp->ofill >= bp->head->b + sizeof bp->head->b) {
                Overflow_p op = (Overflow_p) malloc(sizeof(Overflow));
                op->next = bp->head;
                bp->head = op;
                bp->ofill = op->b;
            }
            memcpy(bp->ofill, bp->buf, sizeof bp->buf);
            bp->ofill += sizeof bp->buf;
            bp->saved += sizeof bp->buf;
            n = bp->fill.c - bp->slack;
            memcpy(bp->buf, bp->slack, n);
            bp->fill.c = bp->buf + n;
        }
        n = len;
        if (n > bp->limit - bp->fill.c)
            n = bp->limit - bp->fill.c; /* TODO: copy big chunks to overflow */
        memcpy(bp->fill.c, data, n);
        bp->fill.c += n;
        data = (const char*) data + n;
        len -= n;
    }
}

int NextBuffer (Buffer_p bp, char **firstp, int *countp) {
    int count;
    
    if (*firstp == 0) {
        Overflow_p p = bp->head, q = 0;
        while (p != 0) {
            Overflow_p t = p->next;
            p->next = q;
            q = p;
            p = t;
        }
        
        bp->head = q;
        bp->used = 0;
    } else if (*firstp == bp->head->b) {
        bp->head = bp->head->next;
        bp->used += *countp;
        free(*firstp);
    } else if (*firstp == bp->buf)
        return *countp == 0;
    
    if (bp->head != 0) {
        *firstp = bp->head->b;
        count = bp->saved - bp->used;
        if (count > (int) sizeof bp->head->b)
            count = sizeof bp->head->b;
    } else {
        count = bp->fill.c - bp->buf;
        *firstp = bp->buf;
    }
    
    *countp = count;
    return *countp;
}

void *BufferAsPtr (Buffer_p bp, int fast) {
    intptr_t len;
    char *data, *ptr = 0;
    int cnt;

    if (fast && bp->saved == 0)
        return bp->buf;

    len = BufferFill(bp);
    if (bp->result == 0)
        bp->result = malloc(len);
        
    for (data = bp->result; NextBuffer(bp, &ptr, &cnt); data += cnt)
        memcpy(data, ptr, cnt);

    return bp->result;
}