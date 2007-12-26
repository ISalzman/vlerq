/*  Tcl extension binding.
    $Id$
    This file is part of Vlerq, see base/vlerq.h for full copyright notice. */

#define USE_TCL_STUBS 1
#include <tcl.h>

/* shorthand */
#define _ptr1   internalRep.twoPtrValue.ptr1
#define _ptr2   internalRep.twoPtrValue.ptr2
#define TclAppend(list,elem)    Tcl_ListObjAppendElement(NULL,list,elem)

#include "lvq.c"

/* stub interface code, removes the need to link with libtclstub*.a */
#if defined(STATIC_BUILD)
#define MyInitStubs(x) 1
#else
#include "stubs.h"
#endif

/*  Define a custom "tvqobj" type for Tcl objects, containing a reference to a
    Lua object.  The "L" state pointer is stored as twoPtrValue.ptr1, the int
    reference is stored as twoPtrValue.ptr2 (via a cast).  Calls Lua's unref
    when the Tcl_Obj is deleted.  The lifetime of the referenced Lua object is
    therefore tied to the Tcl_Obj's one.
    
    There is a string representation which is only for debugging - once the
    internal rep shimmers away, there is no way to get back the Lua object. */

/* forward */
static Tcl_ObjType f_tvqObjType;
static vq_View (ObjAsView) (Tcl_Interp *interp, Tcl_Obj *obj);
static Tcl_Obj* (ViewAsList) (vq_View view);
static Object_p (ItemAsObj) (vq_Type type, vq_Cell item);

static void FreeLuaIntRep (Tcl_Obj *obj) {
    lua_State *L = obj->_ptr1;
    int ref = (int) obj->_ptr2;
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
}

static void DupLuaIntRep (Tcl_Obj *src, Tcl_Obj *obj) {
    puts("DupLuaIntRep called!"); /* could be implemented, no need so far */
}

static vq_View ViewFromTvqObj (Tcl_Obj *obj) {
    vq_View view;
    lua_State *L = obj->_ptr1;
    int ref = (int) obj->_ptr2;
    assert(obj->typePtr == &f_tvqObjType);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    view = checkview(L, lua_gettop(L));
    lua_pop(L, 1);
    return view;
}

static void UpdateLuaStrRep (Tcl_Obj *obj) {
#if 1 /* FIXME: crash when converting via ViewAsList */
    char buf[50];
    int n = sprintf(buf, "tvqobj: %p %d", obj->_ptr1, (int) obj->_ptr2);
    obj->bytes = strcpy(malloc(n+1), buf);
    obj->length = n;
#else
    printf("ulsr: %p %d\n", obj->_ptr1, (int) obj->_ptr2);
{
    /* TODO: try to avoid extra string/list rep buffering and copying */
    Tcl_Obj* list = ViewAsList(ViewFromTvqObj(obj));
    const char* str = Tcl_GetStringFromObj(list, &obj->length);
    obj->bytes = strcpy(Tcl_Alloc(obj->length+1), str);
    Tcl_DecrRefCount(list);
}
#endif
}

static int SetLuaFromAnyRep (Tcl_Interp *interp, Tcl_Obj *obj) {
    puts("SetLuaFromAnyRep called!"); /* conversion to a tvqobj is impossible */
    return TCL_ERROR;
}

static Tcl_ObjType f_tvqObjType = {
    "tvqobj", FreeLuaIntRep, DupLuaIntRep, UpdateLuaStrRep, SetLuaFromAnyRep
};

static Tcl_Obj* WrapAsTvqObj (lua_State *L, int t) {
    Tcl_Obj *obj = Tcl_NewObj();
    Tcl_InvalidateStringRep(obj);
    obj->_ptr1 = L;
    lua_pushvalue(L, t);
    obj->_ptr2 = (void*) luaL_ref(L, LUA_REGISTRYINDEX);
    obj->typePtr = &f_tvqObjType;
    return obj;
}

/*
static Tcl_Obj* UserdataAsTclObj (lua_State *L, int t) {
    if (lua_getmetatable(L, t)) {
        lua_getfield(L, LUA_REGISTRYINDEX, "Vlerq.view");
        if (lua_rawequal(L, -1, -2)) {
            lua_pop(L, 2);
            return ViewAsList(checkview(L, t));
        }
        lua_pop(L, 2);
    }
    return WrapAsTvqObj(L, t);
}
*/

static Tcl_Obj* UnboxTvqObj (lua_State *L, int t) {
    void* p = lua_touserdata(L, t);
    if (p) {
        if (lua_islightuserdata(L, t))
            return p;   
        if (lua_getmetatable(L, t)) {
            lua_getfield(L, LUA_REGISTRYINDEX, "Vlerq.tcl");
            if (lua_rawequal(L, -1, -2)) {
                lua_pop(L, 2);
                return ViewAsList(checkview(L, t));
            }
            lua_pop(L, 2);
        }
    }
    return WrapAsTvqObj(L, t);
}

static Tcl_Obj* LuaAsTclObj (lua_State *L, int t) {
    switch (lua_type(L, t)) {
        case LUA_TNIL:
            return Tcl_NewObj();
        case LUA_TNUMBER: {
            double d = lua_tonumber(L, t);
            long l = (long) d;
            return l == d ? Tcl_NewLongObj(l) : Tcl_NewDoubleObj(d);
        }
        case LUA_TSTRING: {
            int len = lua_strlen(L, t);
            const char* ptr = lua_tostring(L, t);
            return Tcl_NewByteArrayObj((unsigned char*) ptr, len);
        }
        case LUA_TBOOLEAN:
            return Tcl_NewBooleanObj(lua_toboolean(L, t));
        default:
            return UnboxTvqObj(L, t);
    }
}

/*  Define various Tcl conversions from/to Vlerq objects.  All calls below
    named ...As... return the result, while calls ...To... store it in arg. */

vq_View ListAsMetaView (void *interp, Tcl_Obj *obj) {
    int r, rows, objc;
    Tcl_Obj **objv, *entry;
    const char *name, *sep;
    vq_Type type;
    vq_View table;

    if (Tcl_ListObjLength(interp, obj, &rows) != TCL_OK)
        return 0;

    table = vq_new(rows, vMeta(EmptyMetaView()));

    for (r = 0; r < rows; ++r) {
        Tcl_ListObjIndex(NULL, obj, r, &entry);
        if (Tcl_ListObjGetElements(interp, entry, &objc, &objv) != TCL_OK ||
                objc < 1 || objc > 2)
            return 0;

        name = Tcl_GetString(objv[0]);
        sep = strchr(name, ':');
        type = objc > 1 ? VQ_view : VQ_string;

        if (sep != 0) {
            int n = sep - name;
            char *buf = memcpy(malloc(n+1), name, n);
            buf[n] = 0;
            if (sep[1] != 0)
                type = StringAsType(sep+1);
            vq_setString(table, r, 0, buf);
            free(buf);
        } else
            vq_setString(table, r, 0, name);

        vq_setInt(table, r, 1, type);

        if (objc > 1) {
            vq_View t = ListAsMetaView(interp, objv[1]);
            if (t == 0)
                return 0;
            vq_setView(table, r, 2, t);
        } else
            vq_setView(table, r, 2, EmptyMetaView());
    }

    return table;
}

static void AdjustCmdDef (Tcl_Interp *interp, Tcl_Obj *cmd) {
    Tcl_Obj *origname, *newname;
    Tcl_CmdInfo cmdinfo;
    Tcl_Obj *buf[2];

    /* Use "::vops::blah ..." if it exists, else use "::tvq blah ...". */
    /* TODO: could perhaps be optimized with 8.5 ensembles */
     
    Tcl_ListObjIndex(NULL, cmd, 0, &origname);

    /* insert "::vops::" before the first list element */
    newname = Tcl_NewStringObj("::vops::", -1);
    Tcl_AppendObjToObj(newname, origname);
    
    if (Tcl_GetCommandInfo(interp, Tcl_GetString(newname), &cmdinfo))
        Tcl_ListObjReplace(NULL, cmd, 0, 1, 1, &newname);
    else {
        Tcl_DecrRefCount(newname);
        buf[0] = Tcl_NewStringObj("::tvq", -1);
        buf[1] = origname;
        Tcl_ListObjReplace(NULL, cmd, 0, 1, 2, buf);
    }
}

static vq_View ComputeView (Tcl_Interp *interp, Tcl_Obj *cmd, int objc, Tcl_Obj **objv) {
    int ac;
    Tcl_Obj **av;
    vq_View view = NULL;
    Tcl_SavedResult state;

    Tcl_SaveResult(interp, &state);
    Tcl_IncrRefCount(cmd);

    AdjustCmdDef(interp, cmd);
    Tcl_ListObjGetElements(NULL, cmd, &ac, &av);
    /* don't use Tcl_EvalObjEx, it forces a string conversion */
    if (Tcl_EvalObjv(interp, ac, av, TCL_EVAL_GLOBAL) == TCL_OK) {
        /* result to view, may call EvalIndirectView recursively */
        view = ObjAsView(interp, Tcl_GetObjResult(interp));
    }

    Tcl_DecrRefCount(cmd);
    if (view == NULL)
        Tcl_DiscardResult(&state);
    else
        Tcl_RestoreResult(interp, &state);
    return view;
}

static vq_View ObjAsView (Tcl_Interp *interp, Tcl_Obj *obj) {
    int objc, rows = 0;
    Tcl_Obj **objv;
    
    if (obj->typePtr == &f_tvqObjType)
        return ViewFromTvqObj(obj);
    
    if (Tcl_ListObjGetElements(interp, obj, &objc, &objv) != TCL_OK)
        return NULL;

    if (objc == 0 || Tcl_GetIntFromObj(NULL, objv[0], &rows) == TCL_OK)
        return vq_new(rows, NULL);
    
    if (objc == 1)
        return AsMetaVop(Tcl_GetString(objv[0]));

    return ComputeView(interp, Tcl_DuplicateObj(obj), objc, objv);
}

static Tcl_Obj* MetaViewAsList (vq_View meta) {
    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    if (meta != 0) {
        vq_Type type;
        int rowNum;
        vq_View subt;
        Tcl_Obj *fieldobj;
        char buf[30];

        for (rowNum = 0; rowNum < vCount(meta); ++rowNum) {
            fieldobj = Tcl_NewStringObj(vq_getString(meta, rowNum, 0, ""), -1);
            type = vq_getInt(meta, rowNum, 1, VQ_nil);
            switch (type) {
                case VQ_string:
                    break;
                case VQ_view:
                    subt = vq_getView(meta, rowNum, 2, 0);
                    assert(subt != 0);
                    if (vCount(subt) > 0) {
                        fieldobj = Tcl_NewListObj(1, &fieldobj);
                        TclAppend(fieldobj, MetaViewAsList(subt));
                        break;
                    }
                default:
                    Tcl_AppendToObj(fieldobj, ":", 1);
                    Tcl_AppendToObj(fieldobj, TypeAsString(type, buf), 1);
                    break;
            }
            TclAppend(result, fieldobj);
        }
    }
    return result;
}

static Tcl_Obj* ColumnAsList (vq_Cell colref, int rows, int mode) {
    int i;
    Tcl_Obj *list = Tcl_NewListObj(0, NULL);
#if VQ_RANGES_H
    if (mode == 0) {
        Vector ranges = 0;
        for (i = 0; i < rows; ++i) {
            vq_Cell item = colref;
            if (GetItem(i, &item) == VQ_nil)
                RangeFlip(&ranges, i, 1);
        }
        mode = -2;
        rows = vCount(ranges);
        colref.o.a.v = ranges;
    }
#endif
    for (i = 0; i < rows; ++i) {
        vq_Cell item = colref;
        vq_Type type = GetItem(i, &item);
        if (mode < 0 || (mode > 0 && type != VQ_nil))
            TclAppend(list, ItemAsObj(type, item));
        else if (mode == 0 && type == VQ_nil)
            TclAppend(list, Tcl_NewIntObj(i));
    }
#if VQ_RANGES_H
    if (mode == -2)
        vq_release(colref.o.a.v);
#endif
    return list;
}

static Tcl_Obj* VectorAsList (Vector v) {
    vq_Cell item;
    if (v == 0)
        return Tcl_NewObj();
    item.o.a.v = v;
    return ColumnAsList (item, vCount(v), -1);
}

static Tcl_Obj* ChangesAsList (vq_View view) {
    int c, rows = vCount(view), cols = vCount(vq_meta(view));
    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    if (IsMutable(view)) {
        TclAppend(result, Tcl_NewStringObj("mutable", 7));
    /*  TclAppend(result, vOref(view));  */
    /*  TclAppend(result, VectorAsList(vDelv(view)));  */
    /*  TclAppend(result, VectorAsList(vPerm(view)));  */
    /*  TclAppend(result, VectorAsList(vInsv(view)));  */
        TclAppend(result, Tcl_NewObj());
        TclAppend(result, Tcl_NewObj());
        TclAppend(result, Tcl_NewObj());
        TclAppend(result, Tcl_NewObj());
        if (rows > 0)
            for (c = 0; c < cols; ++c) {
                Vector *vecp = (Vector*) vData(view) + 3 * c;
                TclAppend(result, VectorAsList(vecp[0]));
                if (vecp[0] != 0 && vCount(vecp[0]) > 0) {
                    TclAppend(result, VectorAsList(vecp[1]));
                    TclAppend(result, VectorAsList(vecp[2]));
                }
            }
    }
    return result;
}

static Tcl_Obj* MetaAsObj (vq_View meta) {
    Tcl_Obj* obj;
    Buffer buffer;
    InitBuffer(&buffer);
    MetaAsDesc(meta, &buffer);
    ADD_CHAR_TO_BUF(buffer, 0);
    obj = Tcl_NewStringObj(BufferAsPtr(&buffer, 1), BufferFill(&buffer));
    ReleaseBuffer(&buffer, 0);
    return obj;
}

static Tcl_Obj* DataAsList (vq_View view) {
    vq_View meta = vq_meta(view);
    int c, rows = vCount(view), cols = vCount(meta);
    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    TclAppend(result, Tcl_NewStringObj("data", 4));
    TclAppend(result, MetaAsObj(meta));
    TclAppend(result, Tcl_NewIntObj(rows));
    if (rows > 0)
        for (c = 0; c < cols; ++c) {
            int length;
            Tcl_Obj *list = ColumnAsList(view[c], rows, 1);
            TclAppend(result, list);
            Tcl_ListObjLength(NULL, list, &length);
            if (length != 0 && length != rows)
                TclAppend(result, ColumnAsList(view[c], rows, 0));
        }
    return result;
}

static Tcl_Obj* ViewAsList (vq_View view) {
    vq_View meta = vq_meta(view);
    
    if (IsMutable(view))
        return ChangesAsList(view);
    
    if (meta == vq_meta(meta))
        return MetaAsObj(meta);
    
    if (vCount(meta) == 0)
        return Tcl_NewIntObj(vCount(view));

    return DataAsList(view);
}

static Tcl_Obj* ItemAsObj (vq_Type type, vq_Cell item) {
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
        case VQ_view:   if (item.o.a.v == 0)
                            break;
                        return ViewAsList(item.o.a.v);/* FIXME: LuaAsTclObj? */
        case VQ_objref: if (item.o.a.p == 0)
                            break;
                        return item.o.a.p; /* TODO: VQ_objref conv used when? */
    }
    return Tcl_NewObj();
}

static Tcl_Interp* TclInterpreter (lua_State *L) {
    Tcl_Interp *interp;  
    
    /* use the special reference with fixed index #1 for fast access
       this must have been set up when Lua was initilized, see Tvq_Init
       IDEA: could fetch this once in parseargs to reduce # of calls */
    lua_rawgeti(L, LUA_REGISTRYINDEX, 1);
    interp = lua_touserdata(L, -1);
    lua_pop(L, 1);
    
    return interp;    
}
int ObjToItem (void *L, vq_Type type, vq_Cell *item) {
    switch (type) {
        case VQ_nil:    return 1;
        case VQ_int:    return Tcl_GetIntFromObj(NULL, item->o.a.p,
                                                        &item->o.a.i) == TCL_OK;
        case VQ_long:   return Tcl_GetWideIntFromObj(NULL, item->o.a.p,
                                            (Tcl_WideInt*) &item->w) == TCL_OK;
        case VQ_float:
        case VQ_double: if (Tcl_GetDoubleFromObj(NULL, item->o.a.p,
                                                            &item->d) != TCL_OK)
                            return 0;
                        if (type == VQ_float)
                            item->o.a.f = (float) item->d;
                        break;
        case VQ_string: item->o.a.s = Tcl_GetString(item->o.a.p);
                        break;
        case VQ_bytes:  item->o.a.p = Tcl_GetByteArrayFromObj(item->o.a.p,
                                                                &item->o.b.i);
                        break;
        case VQ_view:   item->o.a.v = ObjAsView(TclInterpreter(L), item->o.a.p);
                        return item->o.a.v != NULL;
        case VQ_objref: assert(0); return 0;
    }
    return 1;
}

/*  Callbacks from Lua into Tcl are implemented via the 'tcl' global in lua.
    The arguments are combined and then eval'ed as a command in Tcl. */

static int LuaCallback (lua_State *L) {
    Tcl_Obj *list = Tcl_NewListObj(0, NULL), **av;
    Tcl_Interp* interp = lua_touserdata(L, lua_upvalueindex(1));
    int i, n = lua_gettop(L), ac;
    Tcl_IncrRefCount(list);
    /* TODO: could use a buffer instead */
    for (i = 1; i <= n; ++i)
        Tcl_ListObjAppendElement(interp, list, LuaAsTclObj(L, i));
    /* don't use Tcl_EvalObjEx, it forces a string conversion */
    Tcl_ListObjGetElements(interp, list, &ac, &av);
    i = Tcl_EvalObjv(interp, ac, av, TCL_EVAL_GLOBAL);
    Tcl_DecrRefCount(list);
    if (i == TCL_ERROR)
        luaL_error(L, "tvq: %s", Tcl_GetStringResult(interp));
    return 0;
}

/*  The "tvq" command is the central interface from Tcl into Lua.  It looks up
    its first argument in a global "vops" table and calls that with remaining
    arguments passed on as light userdata pointers.  It is up to the callee
    to then cast each of its arguments to proper Lua values or references. */
    
static int TvqCmd (ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    lua_State *L = data;
    int i, v;
    const char *cmd;

    if (objc < 2) {
      Tcl_WrongNumArgs(interp, objc, objv, "vop ?...?");
      return TCL_ERROR;
    }
    
    cmd = Tcl_GetStringFromObj(objv[1], NULL);
    lua_getglobal(L, "vops");
    lua_getfield(L, -1, cmd); /* TODO: cache f_tvqObjType in objv[1] */
    lua_remove(L, -2);
    if (lua_isnil(L, -1)) {
        Tcl_AppendResult(interp, "not found in vops: ", cmd, NULL);
        return TCL_ERROR;
    }
                
    for (i = 2; i < objc; ++i)
        if (objv[i]->typePtr == &f_tvqObjType)
            lua_rawgeti(L, LUA_REGISTRYINDEX, (int) objv[i]->_ptr2);
        else
            lua_pushlightuserdata(L, objv[i]);

    v = lua_pcall(L, objc-2, 1, 0);
    if (!lua_isnil(L, -1))
        Tcl_SetObjResult(interp, LuaAsTclObj(L, -1));
    lua_pop(L, 1);
    return v == 0 ? TCL_OK : TCL_ERROR;
}

static int PipeCmd (ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    int e = TCL_OK, i, n, index;
    Tcl_Obj *result, *buf[10], **cvec;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "view ?op ...? ?| ...?");
        return TCL_ERROR;
    }
    
    Tcl_SetObjResult(interp, objv[1]); objc -= 2; objv += 2;
    
    while (e == TCL_OK && objc > 0) {
        for (n = 0; n < objc; ++n)
            if (objv[n]->bytes != 0 && *objv[n]->bytes == '|' && 
                    objv[n]->length == 1)
                break;

        if (n > 0) {
            cvec = n > 8 ? malloc((n+2) * sizeof(Tcl_Obj*)) : buf;
                
            if (Tcl_GetIndexFromObjStruct(NULL, *objv,
                                            f_vdispatch, sizeof *f_vdispatch, 
                                            "", TCL_EXACT, &index) != TCL_OK)
                index = -1;

            cvec[0] = Tcl_NewStringObj("vlerq", -1);
            cvec[1] = objv[0];
            cvec[2] = Tcl_GetObjResult(interp);
            Tcl_IncrRefCount(cvec[2]);
            for (i = 1; i < n; ++i)
                cvec[i+2] = objv[i];
            
            result = Tcl_NewListObj(n+1, cvec+1);

            if (index < 0 || *f_vdispatch[index].args != 'V') {
                int ac;
                Tcl_Obj **av;
                AdjustCmdDef(interp, result);
                Tcl_ListObjGetElements(NULL, result, &ac, &av);
                /* don't use Tcl_EvalObjEx, it forces a string conversion */
                e = Tcl_EvalObjv(interp, ac, av, 0);
                Tcl_DecrRefCount(result);
            } else
                Tcl_SetObjResult(interp, result);
            
            Tcl_DecrRefCount(cvec[2]);
            if (n > 8)
                free(cvec);
        }
        
        objc -= n+1; objv += n+1; /*++k;*/
    }

    return e;
}

static int StringLookup (const char *name, vq_Cell col) {
    return -1;
}

static Tcl_Obj* BufferAsTclList (Buffer* bp) {
    int argc;
    Tcl_Obj *result;
    
    argc = BufferFill(bp) / sizeof(Tcl_Obj*);
    result = Tcl_NewListObj(argc, BufferAsPtr(bp, 1));
    ReleaseBuffer(bp, 0);
    return result;
}

static Tcl_Obj* GetViewRows (vq_View view, int row, int rows, int tags) {
    int r, c, cols;
    vq_View meta;
    vq_Cell item;
    struct Buffer buf;
    
    meta = vMeta(view);
    cols = vCount(meta);
    InitBuffer(&buf);
    
    for (r = 0; r < rows; ++r)
        for (c = 0; c < cols; ++c) {
            if (tags) {
                item = meta[0];
                ADD_PTR_TO_BUF(buf, ItemAsObj(GetItem(c, &item), item));
            }
            item = view[c];
            ADD_PTR_TO_BUF(buf, ItemAsObj(GetItem(r + row, &item), item));
        }
        
    return BufferAsTclList(&buf);
}

static int ColumnNumber(Tcl_Interp *interp, Tcl_Obj* obj, vq_View meta) {
    int col;
    const char *name = Tcl_GetString(obj);
    
    switch (*name) {

        case '#': return -2;
        case '*': return -1;
        case '-': case '0': case '1': case '2': case '3': case '4':
                  case '5': case '6': case '7': case '8': case '9':
            if (Tcl_GetIntFromObj(interp, obj, &col) != TCL_OK)
                return -3;

            if (col < 0)
                col += vCount(meta);
                
            if (col < 0 || col >= vCount(meta))
                return -4;
            break;

        default:
            col = StringLookup(name, meta[0]);
            if (col < 0)
                return -3;
    }

    return col;
}

static int GetCmd (ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    vq_View view;
    int i, r, row, col, rows;
    vq_Type currtype;
    vq_Cell item;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "view ?...?");
        return TCL_ERROR;
    }
    
    item.o.a.v = view = ObjAsView(interp, objv[1]);
    currtype = VQ_view;
    
    objc -= 2; objv += 2;
    if (objc == 0) {
        Tcl_SetObjResult(interp, GetViewRows(view, 0, vCount(view), 0));
        return TCL_OK;
    }
    
    for (i = 0; i < objc; ++i) {
        if (currtype == VQ_view)
            view = item.o.a.v;
        else /* TODO: make sure the object does not leak if newly created */
            view = ObjAsView(interp, ItemAsObj(currtype, item));
        
        if (view == NULL)
            return TCL_ERROR;

        rows = vCount(view);

        if (Tcl_GetIntFromObj(0, objv[i], &row) != TCL_OK)
            switch (*Tcl_GetString(objv[i])) {
                case '@':   item.o.a.v = vMeta(view);
                            currtype = VQ_view;
                            continue;
                case '#':   item.o.a.i = rows;
                            currtype = VQ_int;
                            continue;
                case '*':   row = -1;
                            break;
                default:    Tcl_GetIntFromObj(interp, objv[i], &row);
                            return TCL_ERROR;
            }
        else if (row < 0) {
            row += rows;
            if (row < 0) {
                Tcl_SetResult(interp, "row is out of range", TCL_STATIC);
                return TCL_ERROR;
            }
        }

        if (++i >= objc) {
            if (row >= 0)
                item.o.a.p = GetViewRows(view, row, 1, 1); /* one tagged row */
            else {
                struct Buffer buf;
                InitBuffer(&buf);
                for (r = 0; r < rows; ++r)
                    ADD_PTR_TO_BUF(buf, GetViewRows(view, r, 1, 0));
                item.o.a.p = BufferAsTclList(&buf);
            }
            Tcl_SetObjResult(interp, item.o.a.p);
            return TCL_OK;
        }

        col = ColumnNumber(interp, objv[i], vMeta(view));

        if (row >= 0)
            switch (col) {
                default:    item = view[col];
                            currtype = GetItem(row, &item);
                            break;
                case -1:    item.o.a.p = GetViewRows(view, row, 1, 0);
                            currtype = VQ_objref;
                            break;
                case -2:    item.o.a.i = row;
                            currtype = VQ_int;
                            break;
                case -3:    return TCL_ERROR;
                case -4:    Tcl_SetResult(interp, "column is out of range",
                                                                TCL_STATIC);
                            return TCL_ERROR;
            }
        else
            switch (col) {
                default:    item = view[col];
/*
                            currtype = VQ_column;
*/
                            break;
                case -1:    item.o.a.p = GetViewRows(view, 0, rows, 0);
                            currtype = VQ_objref;
                            break;
                case -2:    assert(0);
/*
                            args->c = NewIotaColumn(rows);
                            currtype = VQ_column;
*/
                            break;
                case -3:    return TCL_ERROR;
                case -4:    Tcl_SetResult(interp, "column is out of range",
                                                                TCL_STATIC);
                            return TCL_ERROR;
            }
    }
    
    Tcl_SetObjResult(interp, ItemAsObj(currtype, item));
    return TCL_OK;
}

static int tclobj_gc (lua_State *L) {
    Tcl_Obj *p = lua_touserdata(L, -1); assert(p != NULL);
    Tcl_DecrRefCount(p);
    return 0;
}

static int TvqDoLua (lua_State *L) {
    LVQ_ARGS(L,A,"S");
    if (luaL_loadstring(L, A[0].o.a.s))
        lua_error(L);
    lua_call(L, 0, 1);
    return 1;
}

DLLEXPORT int Tvq_Init (Tcl_Interp *interp) {
    lua_State *L;
    int ipref;
    
    if (!MyInitStubs(interp) || Tcl_PkgRequire(interp, "Tcl", "8.4", 0) == NULL)
        return TCL_ERROR;

    /* set up a new instance on Lua, make it clean up when Tcl finishes */
    L = lua_open();
    Tcl_CreateExitHandler((Tcl_ExitProc*) lua_close, L);
    
    /* first off, store a pointer to the Tcl interpreter as reference #1 */
  	lua_pushlightuserdata(L, interp);
    ipref = luaL_ref(L, LUA_REGISTRYINDEX);
    assert(ipref == 1); /* make sure it really is 1, see TclInterpreter() */

    luaL_openlibs(L); /* TODO: do we need all the standard Lua libs? */

    /* register a custom Vlerq.tcl userdata type to hold on to Tcl objects */
    luaL_newmetatable(L, "Vlerq.tcl");
    lua_pushcfunction(L, tclobj_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    /* initialize lvq */
    lua_pushcfunction(L, luaopen_lvq_core);
    lua_pushstring(L, "lvq.core");
    lua_call(L, 1, 0);

    /* register a Tcl callback as Lua global "tcl" */
    lua_pushlightuserdata(L, interp);
  	lua_pushcclosure(L, LuaCallback, 1);
    lua_setglobal(L, "tcl");

    /* define a "dostring" vop to evaluate a string in Lua */
    lua_getglobal(L, "vops");
    lua_pushcfunction(L, TvqDoLua);
    lua_setfield(L, -2, "dostring");
    lua_pop(L, 1);

    Tcl_CreateObjCommand(interp, "tvq", TvqCmd, L, NULL);
    Tcl_CreateObjCommand(interp, "tvq::pipe", PipeCmd, L, NULL);
    Tcl_CreateObjCommand(interp, "tvq::get", GetCmd, L, NULL);
    
    assert(strcmp(PACKAGE_VERSION, VQ_RELEASE) == 0);
    return Tcl_PkgProvide(interp, "tvq", PACKAGE_VERSION);
}
