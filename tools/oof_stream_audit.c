#define _GNU_SOURCE
#include <link.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned long call_count = 0;
static unsigned long call_limit = 20000;
static int use_plt = 0;

static int contains(const char *text, const char *needle) {
    return text && needle && strstr(text, needle) != 0;
}

static void write_str(const char *text) {
    if (text) {
        write(2, text, strlen(text));
    }
}

static void write_line2(const char *prefix, const char *text) {
    write_str(prefix);
    write_str(text && *text ? text : "<null>");
    write_str("\n");
}

static int interesting_object(const char *name) {
    return contains(name, "core85.so") ||
           contains(name, "mngcore.so") ||
           contains(name, "mngbase.so") ||
           contains(name, "mngui.so") ||
           contains(name, "dsgnfrm.so") ||
           contains(name, "frmcore.so") ||
           contains(name, "basic.so") ||
           contains(name, "basicui.so") ||
           contains(name, "wbase") ||
           contains(name, "1cv8");
}

static int interesting_symbol(const char *name) {
    return contains(name, "_ZN4core12ListInStream") ||
           contains(name, "_ZN4core13ListOutStream") ||
           contains(name, "_ZN4core17TypeDomainPattern") ||
           contains(name, "_ZNK4core17TypeDomainPattern") ||
           contains(name, "_ZN4core11CompositeID") ||
           contains(name, "_ZNK4core11CompositeID") ||
           contains(name, "_ZN4core12GenericValue") ||
           contains(name, "_ZNK4core12GenericValue") ||
           contains(name, "_ZN4core15FormattedString") ||
           contains(name, "_ZNK4core15FormattedString") ||
           contains(name, "_ZN4core12LocalWString") ||
           contains(name, "_ZNK4core12LocalWString") ||
           contains(name, "_ZN4core5Color") ||
           contains(name, "_ZN4core4Font") ||
           contains(name, "_ZN4core9V8Picture") ||
           contains(name, "ListInStream") ||
           contains(name, "ListOutStream") ||
           contains(name, "TypeDomainPattern") ||
           contains(name, "CompositeID") ||
           contains(name, "GenericValue") ||
           contains(name, "FormattedString") ||
           contains(name, "LocalWString") ||
           contains(name, "PersistenceStorage") ||
           contains(name, "cf_form_controls") ||
           contains(name, "create_composite_id_val") ||
           contains(name, "FormDocument") ||
           contains(name, "FormDes") ||
           contains(name, "LogForm");
}

unsigned int la_version(unsigned int version) {
    (void)version;
    const char *limit = getenv("OOF_AUDIT_CALL_LIMIT");
    const char *plt = getenv("OOF_AUDIT_PLT");
    if (limit && *limit) {
        call_limit = strtoul(limit, 0, 10);
    }
    use_plt = plt && *plt && strcmp(plt, "0") != 0;
    write_line2("OOF_STREAM_AUDIT_VERSION ", "3");
    return LAV_CURRENT;
}

void la_activity(uintptr_t *cookie, unsigned int flag) {
    (void)cookie;
    (void)flag;
}

char *la_objsearch(const char *name, uintptr_t *cookie, unsigned int flag) {
    (void)cookie;
    (void)flag;
    return (char *)name;
}

void la_preinit(uintptr_t *cookie) {
    (void)cookie;
}

unsigned int la_objopen(struct link_map *map, Lmid_t lmid, uintptr_t *cookie) {
    (void)lmid;
    *cookie = (uintptr_t)map;
    if (interesting_object(map->l_name)) {
        write_line2("OOF_STREAM_AUDIT_OBJ ", map->l_name);
    }
    return LA_FLG_BINDTO | LA_FLG_BINDFROM;
}

unsigned int la_objclose(uintptr_t *cookie) {
    (void)cookie;
    return 0;
}

uintptr_t la_symbind64(Elf64_Sym *sym, unsigned int ndx, uintptr_t *refcook,
                       uintptr_t *defcook, unsigned int *flags,
                       const char *symname) {
    (void)ndx;
    (void)refcook;
    (void)defcook;
    (void)flags;
    if (call_count < call_limit && interesting_symbol(symname)) {
        ++call_count;
        write_line2("OOF_STREAM_AUDIT_BIND ", symname);
    }
    return sym->st_value;
}

Elf64_Addr la_x86_64_gnu_pltenter(Elf64_Sym *sym, unsigned int ndx,
                                  uintptr_t *refcook, uintptr_t *defcook,
                                  La_x86_64_regs *regs, unsigned int *flags,
                                  const char *symname, long int *framesizep) {
    (void)ndx;
    (void)refcook;
    (void)defcook;
    (void)regs;
    (void)flags;
    (void)framesizep;
    if (use_plt && call_count < call_limit && interesting_symbol(symname)) {
        ++call_count;
        write_line2("OOF_STREAM_AUDIT_PLT ", symname);
    }
    return sym->st_value;
}

unsigned int la_x86_64_gnu_pltexit(Elf64_Sym *sym, unsigned int ndx,
                                   uintptr_t *refcook, uintptr_t *defcook,
                                   const La_x86_64_regs *inregs,
                                   La_x86_64_retval *outregs,
                                   const char *symname) {
    (void)sym;
    (void)ndx;
    (void)refcook;
    (void)defcook;
    (void)inregs;
    (void)outregs;
    (void)symname;
    return 0;
}
