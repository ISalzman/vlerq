/* Vlerq extension for Tcl */

#define USE_TCL_STUBS 1
#include <tcl.h>

/* stub interface code, removes the need to link with libtclstub*.a */
#if STATIC_BUILD
#define MyInitStubs(x) 1
#else
#include "stubs.h"
#endif

#include "vqdefs.h"
#include "vqcore.c"
#include "vqmem.c"
#include "vqnew.c"
#include "vqwrap.c"

static Tcl_Interp *context; /* TODO: not threadsafe */

/* forward */
extern Tcl_ObjType f_tableObjType;
static Tcl_Obj *(ItemAsObj) (vq_Type type, vq_Item item);
static Tcl_Obj *(TableAsList) (vq_Table table);
static vq_Table (CmdAsTable) (Tcl_Obj *obj);
EXTERN int Vq_Init (Tcl_Interp *interp);

static int StringAsType (const char *str) {
    int type = 0;
    switch (*str & ~0x20) {
        default:    assert(0);
        case 'N':   type = VQ_nil;    break;
        case 'I':   type = VQ_int;    break;
        case 'L':   type = VQ_long;   break;
        case 'F':   type = VQ_float;  break;
        case 'D':   type = VQ_double; break;
        case 'S':   type = VQ_string; break;
        case 'B':   type = VQ_bytes;  break;
        case 'T':   type = VQ_table;  break;
        case 'O':   type = VQ_object; break;
    }
    if (*str & 0x20)
        type |= VQ_NULLABLE;
    while (*++str != 0)
        if ('a' <= *str && *str <= 'z')
        type |= 1 << (*str - 'a' + 5);
    return type;
}
static const char* TypeAsString (int type, char *buf) {
    char c, *p = buf; /* buffer should have room for at least 28 bytes */
    *p++ = VQ_TYPES[type&VQ_TYPEMASK];
    if (type & VQ_NULLABLE)
        p[-1] |= 0x20;
    for (c = 'a'; c <= 'z'; ++c)
        if (type & (1 << (c - 'a' + 5)))
            *p++ = c;
    *p = 0;
    return buf;
}

Object_p ObjIncRef (Object_p obj) {
    if (obj != 0) {
        Tcl_IncrRefCount(obj);
    }
    return obj;
}
void ObjDecRef (Object_p obj) {
    if (obj != 0) {
        Tcl_DecrRefCount(obj);
    }
}

#pragma mark - CUSTOM TABLE OBJECT TYPE -

static void FreeTableIntRep (Tcl_Obj *obj) {
    vq_release(obj->internalRep.twoPtrValue.ptr1);
    ObjDecRef(obj->internalRep.twoPtrValue.ptr2);
}
static void DupTableIntRep (Tcl_Obj *src, Tcl_Obj *obj) {
    obj->internalRep = src->internalRep;
    obj->typePtr = src->typePtr;
    vq_retain(obj->internalRep.twoPtrValue.ptr1);
    ObjIncRef(obj->internalRep.twoPtrValue.ptr2);
}
static void UpdateTableStrRep (Tcl_Obj *obj) {
    int len;
    const char *str;
    Tcl_Obj *list = obj->internalRep.twoPtrValue.ptr2;

    if (list == 0)
        list = TableAsList(obj->internalRep.twoPtrValue.ptr1);
    /* TODO: avoid copying string and then deleting the original */
    str = Tcl_GetStringFromObj(list, &len);
    obj->bytes = strcpy(malloc(len+1), str);
    obj->length = len;
    if (obj->internalRep.twoPtrValue.ptr2 == 0)
        ObjDecRef(list);
}
static void InvalidateNonTableReps (Tcl_Obj *obj) {
    assert(obj->typePtr == &f_tableObjType);
    if (obj->internalRep.twoPtrValue.ptr2 != 0) {
        ObjDecRef(obj->internalRep.twoPtrValue.ptr2);
        obj->internalRep.twoPtrValue.ptr2 = 0;
    }
    Tcl_InvalidateStringRep(obj);
}
static Tcl_Obj *MakeMutableObj (Tcl_Obj *obj) {
    vq_Table t;
    if (Tcl_IsShared(obj))
        obj = Tcl_DuplicateObj(obj);
    t = ObjAsTable(obj);
    assert(obj->typePtr == &f_tableObjType);
    if (!IsMutable(t) || vRefs(t) > 1)
        t = WrapMutable(t);
    if (t != obj->internalRep.twoPtrValue.ptr1) {
        vq_release(obj->internalRep.twoPtrValue.ptr1);
        obj->internalRep.twoPtrValue.ptr1 = vq_retain(t);
    }
    InvalidateNonTableReps(obj);
    return obj;
}
void UpdateVar (vq_Item info) {
    Tcl_Obj *obj = info.o.b.p;
    const char *s = Tcl_GetString(info.o.a.p);
    assert(s[0] == '@');
    InvalidateNonTableReps(obj);
    Tcl_SetVar2Ex(context, s+1, 0, obj, TCL_LEAVE_ERR_MSG | TCL_GLOBAL_ONLY);
}
static vq_Table RefAsTable (Tcl_Obj *obj) {
    Tcl_Obj *o;
    const char *s = Tcl_GetString(obj);
    if (*s != '@')
        return 0;
    o = Tcl_GetVar2Ex(context, s+1, 0, TCL_LEAVE_ERR_MSG | TCL_GLOBAL_ONLY);
    return o != 0 ? ObjAsTable(o) : 0;
}
static int SetTableFromAnyRep (Tcl_Interp *interp, Tcl_Obj *obj) {
    int nargs, rows;
    Tcl_Obj *list;
    vq_Table table = 0;
    
    if (Tcl_ListObjLength(interp, obj, &nargs) != TCL_OK)
        return TCL_ERROR;
        
    if (nargs == 0)
        table = EmptyMetaTable();
    else if (nargs == 1) {
        if (Tcl_GetIntFromObj(interp, obj, &rows) == TCL_OK && rows >= 0) {
            table = vq_new(0);
            vCount(table) = rows;
        } else
            table = RefAsTable(obj);
    }
    
    if (table == 0)
        table = CmdAsTable(obj);
        
    /* TODO: try avoid this wasteful list duplication + deletion */
    list = Tcl_DuplicateObj(obj);    
    
    if (obj->typePtr != 0 && obj->typePtr->freeIntRepProc != 0)
        obj->typePtr->freeIntRepProc(obj);
        
    obj->internalRep.twoPtrValue.ptr1 = vq_retain(table);
    obj->internalRep.twoPtrValue.ptr2 = ObjIncRef(list);
    obj->typePtr = &f_tableObjType;
    return TCL_OK;
}
Tcl_ObjType f_tableObjType = {
    "table", FreeTableIntRep, DupTableIntRep,
        UpdateTableStrRep, SetTableFromAnyRep
};
static Tcl_Obj *TableAsObj (vq_Table table) {
    Tcl_Obj *obj = Tcl_NewObj();
    Tcl_InvalidateStringRep(obj);
    obj->internalRep.twoPtrValue.ptr1 = vq_retain(table);
    obj->internalRep.twoPtrValue.ptr2 = 0;
    obj->typePtr = &f_tableObjType;
    return obj;
}

#pragma mark - CONVERT FROM TCL OBJECTS -

#if 0
vq_Table ObjAsMetaTable (Tcl_Obj *obj) {
    vq_Table emt, meta;
    int r, rows, objc;
    Tcl_Obj *entry, **objv;
    const char *name, *sep, *type;
    char *name2;

    if (Tcl_ListObjLength(0, obj, &rows) != TCL_OK)
        return 0;

    emt = EmptyMetaTable();
    meta = vq_new(vMeta(emt));
    vCount(meta) = rows; /* no, use mutable! */
    for (r = 0; r < rows; ++r) {
        if (Tcl_ListObjIndex(0, obj, r, &entry) != TCL_OK
                || Tcl_ListObjGetElements(0, entry, &objc, &objv) != TCL_OK
                || objc < 1 || objc > 2)
            return 0;

        name = Tcl_GetString(objv[0]);
        fprintf(stderr,"n: %s\n", name);
        sep = strchr(name, ':');
        type = objc > 1 ? "T" : "S";

        if (sep != 0) {
            if (sep[1] != 0) {
                if (strchr("BDFLISVT", sep[1]) == 0)
                    return 0;
                type = sep + 1;
            }
            /* TODO: fix this crummy string truncate logic */
            name2 = strcpy(malloc(strlen(name)+1), name);
            name2[sep-name] = 0;
            Vq_setString(meta, r, 0, name2);
            free(name2);
        } else
            Vq_setString(meta, r, 0, name);
        
        Vq_setInt(meta, r, 1, StringAsType(type));
        
        if (objc > 1) {
            vq_Table sub = ObjAsMetaTable(objv[1]);
            if (sub == 0)
                return 0;
            Vq_setTable(meta, r, 2, sub);
        } else
            Vq_setTable(meta, r, 2, emt);
    }
    return meta;
}
#endif

vq_Table ObjAsTable (Object_p obj) {
    return Tcl_ConvertToType(context, obj, &f_tableObjType) == TCL_OK ?
                ((Tcl_Obj*) obj)->internalRep.twoPtrValue.ptr1 : 0;
}
int ObjToItem (vq_Type type, vq_Item *item) {
    switch (type) {
        case VQ_int:
            return Tcl_GetIntFromObj(context, item->o.a.p,
                                                &item->o.a.i) == TCL_OK;
        case VQ_long:
            return Tcl_GetWideIntFromObj(context, item->o.a.p,
                                            (Tcl_WideInt*) &item->w) == TCL_OK;
        case VQ_float:
        case VQ_double:
            if (Tcl_GetDoubleFromObj(context, item->o.a.p, &item->d) != TCL_OK)
                return 0;
            if (type == VQ_float)
                item->o.a.f = (float) item->d;
            break;
        case VQ_string:
            item->o.a.s = Tcl_GetString(item->o.a.p);
            break;
        case VQ_bytes:
            item->o.a.p = Tcl_GetByteArrayFromObj(item->o.a.p, &item->o.b.i);
            break;
        case VQ_table:
            item->o.a.m = ObjAsTable(item->o.a.p);
            return item->o.a.m != 0;
        case VQ_object:
            break;
        case VQ_nil:
            return 0;
    }
    return 1;
}
static int CastObjToItem (const char *type, vq_Item *item) {
    switch (*type) {
        case 'M': {
            Tcl_Obj *o;
            const char *s = Tcl_GetString(item->o.a.p);
            assert(s[0] == '@');
            o = Tcl_GetVar2Ex(context, s+1, 0,
                                TCL_LEAVE_ERR_MSG | TCL_GLOBAL_ONLY);
            if (o != 0)
                o = MakeMutableObj(o);
            item->o.b.p = o; /* TODO: check for leaks when o is a duplicate */
            return o != 0;
        }
        default:
            return ObjToItem(StringAsType(type) & VQ_TYPEMASK, item);
    }
}
static Vector ListAsIntVec (Tcl_Obj *obj) {
    Vector v;
    int i, n, *ivec;
    if (Tcl_ListObjLength(context, obj, &n) != TCL_OK)
        return 0;
    v = vq_retain(AllocDataVec(VQ_int, n)); /* FIXME: crashes with vq_hold */
    vCount(v) = n;
    ivec = (int*) v;
    for (i = 0; i < n; ++i) {
        Tcl_Obj *entry;
        Tcl_ListObjIndex(0, obj, i, &entry);
        if (Tcl_GetIntFromObj(context, entry, ivec + i) != TCL_OK)
            return 0;
    }
    return v;
}

#pragma mark - CONVERT TO TCL OBJECTS -

static Tcl_Obj *ColumnAsList (vq_Item colref, int rows, int mode) {
    /* TODO: return a custom type instead of converting to list right away */
    vq_Type type;
    vq_Item item;
    int i;
    Tcl_Obj *list = Tcl_NewListObj(0, 0);
    if (mode == 0) {
        Vector ranges = 0;
        for (i = 0; i < rows; ++i) {
            item = colref;
            if (GetItem(i, &item) == VQ_nil)
                RangeFlip(&ranges, i, 1);
        }
        mode = -1;
        rows = vCount(ranges);
        colref.o.a.m = ranges;
    }
    for (i = 0; i < rows; ++i) {
        item = colref;
        type = GetItem(i, &item);
        if (mode < 0 || (mode > 0 && type != VQ_nil))
            Tcl_ListObjAppendElement(0, list, ItemAsObj(type, item));
        else if (mode == 0 && type == VQ_nil)
            Tcl_ListObjAppendElement(0, list, Tcl_NewIntObj(i));
    }
    return list;
}
static Tcl_Obj *MetaTableAsList (vq_Table meta) {
    Tcl_Obj *result = Tcl_NewListObj(0, 0);
    if (meta != 0) {
        vq_Type type;
        int rowNum;
        vq_Table subv;
        Tcl_Obj *fieldobj;
        char buf[30];

        for (rowNum = 0; rowNum < vCount(meta); ++rowNum) {
            fieldobj = Tcl_NewStringObj(Vq_getString(meta, rowNum, 0, ""), -1);
            type = Vq_getInt(meta, rowNum, 1, VQ_nil);
            switch (type) {
                case VQ_string:
                    break;
                case VQ_table:
                    subv = Vq_getTable(meta, rowNum, 2, 0);
                    if (subv != 0) {
                        fieldobj = Tcl_NewListObj(1, &fieldobj);
                        Tcl_ListObjAppendElement(0, fieldobj,
                                                        MetaTableAsList(subv));
                        break;
                    }
                default:
                    Tcl_AppendToObj(fieldobj, ":", 1);
                    Tcl_AppendToObj(fieldobj, TypeAsString(type, buf), 1);
                    break;
            }
            Tcl_ListObjAppendElement(0, result, fieldobj);
        }
    }
    return result;
}
static Tcl_Obj *TableAsList (vq_Table table) {
    vq_Table meta = vq_meta(table);
    int c, rows = vCount(table), cols = vCount(meta);
    Tcl_Obj *result = Tcl_NewListObj(0, 0);

    if (meta == vq_meta(meta)) {
        if (rows > 0) {
            Tcl_ListObjAppendElement(0, result, Tcl_NewStringObj("mdef", 4));
            Tcl_ListObjAppendElement(0, result, MetaTableAsList(table));
        }
    } else if (cols == 0) {
        Tcl_ListObjAppendElement(0, result, Tcl_NewIntObj(rows));
    } else {
        Tcl_ListObjAppendElement(0, result, Tcl_NewStringObj("data", 4));
        Tcl_ListObjAppendElement(0, result, MetaTableAsList(meta));
        Tcl_ListObjAppendElement(0, result, Tcl_NewIntObj(rows));
        if (rows > 0)
            for (c = 0; c < cols; ++c) {
                int length;
                Tcl_Obj *list = ColumnAsList(table[c], rows, 1);
                Tcl_ListObjAppendElement(0, result, list);
                Tcl_ListObjLength(0, list, &length);
                if (length != 0 && length != rows) {
                    list = ColumnAsList(table[c], rows, 0);
                    Tcl_ListObjAppendElement(0, result, list);
                }
            }
    }

    return result;
}
static Tcl_Obj *ItemAsObj (vq_Type type, vq_Item item) {
    switch (type) {
        case VQ_nil:    break;
        case VQ_int:    return Tcl_NewIntObj(item.o.a.i);
        case VQ_long:   return Tcl_NewWideIntObj(item.w);
        case VQ_float:  return Tcl_NewDoubleObj(item.o.a.f);
        case VQ_double: return Tcl_NewDoubleObj(item.d);
        case VQ_string: if (item.o.a.s == 0)
                            break;
                        return Tcl_NewStringObj(item.o.a.s, -1);
        case VQ_bytes:  if (item.o.a.p == 0)
                            break;
                        return Tcl_NewByteArrayObj(item.o.a.p, item.o.b.i);
        case VQ_table:  if (item.o.a.m == 0)
                            break;
                        return TableAsObj(item.o.a.m);
        case VQ_object: return item.o.a.p;
    }
    return Tcl_NewObj();
}

#pragma mark - DELAYED EVALUATION -

static void AdjustCmdDef (Tcl_Obj *cmd) {
    Tcl_Obj *origname, *newname;
    Tcl_CmdInfo cmdinfo;

    /* Use "::vlerq::blah ..." if it exists, else use "vlerq blah ...". */
    /* Could perhaps be simplified (optimized?) by using 8.5 ensembles. */
     
    Tcl_ListObjIndex(0, cmd, 0, &origname);

    /* insert "::vlerq::" before the first list element */
    newname = Tcl_NewStringObj("::vq::", -1);
    Tcl_AppendObjToObj(newname, origname);
    
    if (Tcl_GetCommandInfo(context, Tcl_GetString(newname), &cmdinfo))
        Tcl_ListObjReplace(0, cmd, 0, 1, 1, &newname);
    else {
        Tcl_Obj *buf[2];
        ObjDecRef(newname);
        buf[0] = Tcl_NewStringObj("::vq", -1);
        buf[1] = origname;
        Tcl_ListObjReplace(0, cmd, 0, 1, 2, buf);
    }
}
static vq_Table CmdAsTable (Tcl_Obj *obj) {
    int ac;
    Tcl_Obj **av, *cmd;
    Tcl_SavedResult state;
    vq_Table result = 0;
    
    Tcl_SaveResult(context, &state);
    
    cmd = ObjIncRef(Tcl_DuplicateObj(obj));
    
    AdjustCmdDef(cmd);
    Tcl_ListObjGetElements(0, cmd, &ac, &av);
    /* don't use Tcl_EvalObjEx, it forces a string conversion */
    if (Tcl_EvalObjv(context, ac, av, TCL_EVAL_GLOBAL) == TCL_OK)
        result = ObjAsTable(Tcl_GetObjResult(context));
    
    ObjDecRef(cmd);
    
    if (result == 0)
        Tcl_DiscardResult(&state);
    else
        Tcl_RestoreResult(context, &state);
    return result;
}

#pragma mark - TCL COMMAND INTERFACE -

Object_p DebugCode (Object_p cmd, int objc, Object_p objv[]) {
#ifdef NDEBUG
    return 0;
#else
    static const char *cmds[] = {
        "rflip", "rlocate", "rinsert", "rdelete", "version", 0 
    };
    int index;
    if (Tcl_GetIndexFromObj(context, cmd, cmds, "option", 0, &index) != TCL_OK)
        return 0;
    switch (index) {
        case 0: /* rflip */ {
            vq_Item item;
            int offset, count;
            item.o.a.m = ListAsIntVec(objv[0]);
            if (item.o.a.m == 0)
                return 0;
            if (Tcl_GetIntFromObj(context, objv[1], &offset) != TCL_OK
                    || Tcl_GetIntFromObj(context, objv[2], &count) != TCL_OK)
                return 0;
            RangeFlip(&item.o.a.m, offset, count);
            return ColumnAsList(item, vCount(item.o.a.m), -1);
        }
        case 1: /* rlocate */ {
            Tcl_Obj *result;
            int offset, pos;
            Vector v = ListAsIntVec(objv[0]);
            if (v == 0)
                return 0;
            if (Tcl_GetIntFromObj(context, objv[1], &offset) != TCL_OK)
                return 0;
            pos = RangeLocate(v, offset, &offset);
            result = Tcl_NewListObj(0, 0);
            Tcl_ListObjAppendElement(context, result, Tcl_NewIntObj(pos));
            Tcl_ListObjAppendElement(context, result, Tcl_NewIntObj(offset));
            return result;
        }
        case 2: /* rinsert */ {
            vq_Item item;
            int offset, count, mode;
            item.o.a.m = ListAsIntVec(objv[0]);
            if (item.o.a.m == 0)
                return 0;
            if (Tcl_GetIntFromObj(context, objv[1], &offset) != TCL_OK
                    || Tcl_GetIntFromObj(context, objv[2], &count) != TCL_OK
                    || Tcl_GetIntFromObj(context, objv[3], &mode) != TCL_OK)
                return 0;
            RangeInsert(&item.o.a.m, offset, count, mode);
            return ColumnAsList(item, vCount(item.o.a.m), -1);
        }
        case 3: /* rdelete */ {
            vq_Item item;
            int offset, count;
            item.o.a.m = ListAsIntVec(objv[0]);
            if (item.o.a.m == 0)
                return 0;
            if (Tcl_GetIntFromObj(context, objv[1], &offset) != TCL_OK
                    || Tcl_GetIntFromObj(context, objv[2], &count) != TCL_OK)
                return 0;
            RangeDelete(&item.o.a.m, offset, count);
            return ColumnAsList(item, vCount(item.o.a.m), -1);
        }
        case 4: /* version */
            return Tcl_NewStringObj(VQ_VERSION, -1);
    }
    assert(0);
    return 0;
#endif
}

static int VqObjCmd (ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    int i, index, ok = TCL_ERROR;
    vq_Type type;
    Tcl_Obj *result;
    vq_Item stack [20];
    const char *args;
    char buf[2];
    vq_Pool mypool = vq_addpool();
    
    if (objc <= 1) {
        Tcl_WrongNumArgs(interp, 1, objv, "command ...");
        goto FAIL;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[1], f_commands,
            sizeof *f_commands, "command", TCL_EXACT, &index) != TCL_OK)
        goto FAIL;

    context = interp; /* TODO: not reentrant */
    
    objv += 2; objc -= 2;
    args = f_commands[index].args + 2; /* skip return type and ':' */

    /* TODO: error if there are too many args */
    for (i = 0; args[i] != 0; ++i) {
        assert((size_t) i < sizeof stack / sizeof *stack);
        if (args[i] == 'X') {
            assert(args[i+1] == 0);
            stack[i].o.a.p = (void*) (objv+i);
            stack[i].o.b.i = objc-i;
            break;
        }
        if ((args[i] == 0 && i != objc) || (args[i] != 0 && i >= objc)) {
            char buf [sizeof stack]; /* TODO: use a buffer */
            const char *s;
            *buf = 0;
            for (i = 0; args[i] != 0; ++i) {
                if (*buf != 0)
                    strcat(buf, " ");
                switch (args[i] & ~0x20) {
                    case 'C': s = "list"; break;
                    case 'I': s = "int"; break;
                    case 'N': s = "col"; break;
                    case 'O': s = "any"; break;
                    case 'S': s = "string"; break;
                    case 'T': s = "table"; break;
                    case 'X': s = "..."; break;
                    default: assert(0); s = "?"; break;
                }
                strcat(buf, s);
                if (args[i] & 0x20)
                    strcat(buf, "*");
            }
            Tcl_WrongNumArgs(interp, 2, objv-2, buf);
            goto FAIL;
        }
        stack[i].o.a.p = objv[i];
        buf[0] = args[i];
        buf[1] = 0;
        if (!CastObjToItem(buf, stack+i)) {
            if (*Tcl_GetStringResult(interp) == 0) {
                const char *s = "argument";
                switch (args[i] & ~0x20) {
                    case 'C': s = "list"; break;
                    case 'I': s = "integer"; break;
                    case 'N': s = "column name"; break;
                    case 'T': s = "table"; break;
                }
                Tcl_AppendResult(interp, f_commands[index].name,
                                            ": invalid ", s, 0);
            }
            goto FAIL; /* TODO: append info about which arg is bad */
        }
    }
    
    type = f_commands[index].proc(stack);
    if (f_commands[index].args[0] != 'V') {
        if (type == VQ_nil)
            goto FAIL;
        result = ItemAsObj(type, stack[0]);
        if (result == 0)
            goto FAIL;
        Tcl_SetObjResult(interp, result);
    }
    ok = TCL_OK;

FAIL:
    vq_losepool(mypool);
    return ok;
}

int Vq_Init (Tcl_Interp *interp) {
    if (!MyInitStubs(interp) || Tcl_PkgRequire(interp, "Tcl", "8.4", 0) == 0)
        return TCL_ERROR;
    Tcl_CreateObjCommand(interp, "vq", VqObjCmd, 0, 0);
    return Tcl_PkgProvide(interp, "vq", VQ_VERSION);
}