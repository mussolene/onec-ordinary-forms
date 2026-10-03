#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct Range {
    uintptr_t start;
    uintptr_t end;
} Range;

static Range readable_ranges[2048];
static int readable_range_count = 0;

typedef uintptr_t (*method6_fn)(void *, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef void (*register_all_fn)(void *, void *, void *);

static method6_fn orig_060 = 0;
static method6_fn orig_070 = 0;
static method6_fn orig_080 = 0;
static int inside_dsgnfrm = 0;
static int create_formdocument = 0;
static int proxy_formdocument_factory = 0;
static void *formdocument_factory_vtable_proxy[7];
static void **formdocument_factory_original_vtable = 0;
static void *formdocument_factory_original_create_ptr = 0;

typedef struct Guid {
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t d[8];
} Guid;

static const Guid IID_IUnknown = {0x00000000u, 0x0000u, 0x0000u, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const Guid IID_IFormDocument = {0xda8583a2u, 0xa3ddu, 0x42feu, {0x89, 0x95, 0x1f, 0xf6, 0x4e, 0xaa, 0x59, 0x05}};

typedef uintptr_t (*create_instance_fn)(void *, void *, const Guid *, void **);
typedef uintptr_t (*release_fn)(void *);

static register_all_fn real_register_all(void) {
    static register_all_fn fn = 0;
    if (!fn) {
        fn = (register_all_fn)dlsym(RTLD_NEXT, "_ZN4core11SCOM_Module11registerAllEPNS_12SCOM_ProcessEPNS_19SCOM_ClassRegistrarE");
    }
    return fn;
}

static void refresh_readable_ranges(void) {
    readable_range_count = 0;
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        return;
    }
    char line[512];
    while (readable_range_count < 2048 && fgets(line, sizeof(line), fp)) {
        unsigned long start = 0;
        unsigned long end = 0;
        char perms[8] = {0};
        if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) == 3 && perms[0] == 'r') {
            readable_ranges[readable_range_count].start = (uintptr_t)start;
            readable_ranges[readable_range_count].end = (uintptr_t)end;
            ++readable_range_count;
        }
    }
    fclose(fp);
}

__attribute__((constructor))
static void load_readable_ranges(void) {
    refresh_readable_ranges();
    const char *create_env = getenv("OOF_CREATE_FORMDOCUMENT");
    create_formdocument = create_env && create_env[0] && create_env[0] != '0';
    const char *proxy_env = getenv("OOF_PROXY_FORMDOCUMENT_FACTORY");
    proxy_formdocument_factory = proxy_env && proxy_env[0] && proxy_env[0] != '0';
}

static uintptr_t readable_span_end(uintptr_t value) {
    for (int i = 0; i < readable_range_count; ++i) {
        if (value >= readable_ranges[i].start && value < readable_ranges[i].end) {
            return readable_ranges[i].end;
        }
    }
    return 0;
}

static int readable_bytes(uintptr_t value, size_t size) {
    if (value < 0x10000 || size == 0 || value > UINTPTR_MAX - size) {
        return 0;
    }
    for (int i = 0; i < readable_range_count; ++i) {
        if (value >= readable_ranges[i].start && value + size <= readable_ranges[i].end) {
            return 1;
        }
    }
    return 0;
}

static void guid_text(uintptr_t value, char *out, size_t out_size);

static void object_rtti(void *obj, char *name, size_t name_size,
                        uintptr_t *typeinfo_out, const char **owner_out) {
    if (!name || name_size < 2) {
        return;
    }
    name[0] = 0;
    if (typeinfo_out) {
        *typeinfo_out = 0;
    }
    if (owner_out) {
        *owner_out = "<unknown>";
    }
    uintptr_t vtable_value = 0;
    if (!obj || !readable_bytes((uintptr_t)obj, sizeof(vtable_value))) {
        return;
    }
    memcpy(&vtable_value, obj, sizeof(vtable_value));
    if (!vtable_value || vtable_value < sizeof(void *)) {
        return;
    }
    uintptr_t typeinfo = 0;
    if (!readable_bytes(vtable_value - sizeof(void *), sizeof(typeinfo))) {
        return;
    }
    memcpy(&typeinfo, (const void *)(vtable_value - sizeof(void *)), sizeof(typeinfo));
    if (typeinfo > UINTPTR_MAX - sizeof(void *) * 2 ||
        !readable_bytes(typeinfo, sizeof(void *) * 2)) {
        return;
    }
    uintptr_t name_value = 0;
    memcpy(&name_value, (const void *)(typeinfo + sizeof(void *)), sizeof(name_value));
    uintptr_t span_end = readable_span_end(name_value);
    if (!span_end || name_value >= span_end) {
        return;
    }
    size_t copy_size = span_end - name_value;
    if (copy_size >= name_size) {
        copy_size = name_size - 1;
    }
    if (!copy_size) {
        return;
    }
    memcpy(name, (const void *)name_value, copy_size);
    if (!memchr(name, 0, copy_size)) {
        name[0] = 0;
        return;
    }
    if (typeinfo_out) {
        *typeinfo_out = typeinfo;
    }
    if (owner_out) {
        uintptr_t first_method = 0;
        if (!readable_bytes(vtable_value, sizeof(first_method))) {
            return;
        }
        memcpy(&first_method, (const void *)vtable_value, sizeof(first_method));
        Dl_info info;
        if (first_method && dladdr((void *)first_method, &info) != 0 && info.dli_fname) {
            *owner_out = info.dli_fname;
        }
    }
}

static void log_factory_rtti(uintptr_t class_guid, uintptr_t factory_value) {
    char guid[64];
    char rtti[256];
    const char *owner = "<unknown>";
    uintptr_t typeinfo = 0;
    uintptr_t factory_vtable = 0;
    uintptr_t create_instance = 0;
    uintptr_t create_offset = 0;
    uintptr_t creator_callback = 0;
    uintptr_t creator_offset = 0;
    const char *create_owner = "<unknown>";
    const char *creator_owner = "<unknown>";
    guid_text(class_guid, guid, sizeof(guid));
    object_rtti((void *)factory_value, rtti, sizeof(rtti), &typeinfo, &owner);
    if (readable_bytes(factory_value, sizeof(factory_vtable))) {
        memcpy(&factory_vtable, (const void *)factory_value, sizeof(factory_vtable));
    }
    if (readable_bytes(factory_vtable, sizeof(void *) * 4)) {
        memcpy(&create_instance,
               (const void *)(factory_vtable + sizeof(void *) * 3),
               sizeof(create_instance));
        Dl_info info;
        if (create_instance && dladdr((void *)create_instance, &info) != 0 && info.dli_fname) {
            const char *slash = strrchr(info.dli_fname, '/');
            create_owner = slash ? slash + 1 : info.dli_fname;
            if (info.dli_fbase) {
                create_offset = create_instance - (uintptr_t)info.dli_fbase;
            }
        }
    }
    if (factory_value <= UINTPTR_MAX - 0x38 &&
        readable_bytes(factory_value + 0x38, sizeof(creator_callback))) {
        memcpy(&creator_callback, (const void *)(factory_value + 0x38),
               sizeof(creator_callback));
        Dl_info info;
        if (creator_callback && dladdr((void *)creator_callback, &info) != 0 && info.dli_fname) {
            const char *slash = strrchr(info.dli_fname, '/');
            creator_owner = slash ? slash + 1 : info.dli_fname;
            if (info.dli_fbase) {
                creator_offset = creator_callback - (uintptr_t)info.dli_fbase;
            }
        }
    }
    char buf[700];
    int n = snprintf(buf, sizeof(buf),
                     "OOF_REGISTRATION_RTTI class_guid=%s factory=%p factory_vtable=%p create_instance=%p create_offset=0x%lx create_owner=%s creator_callback=%p creator_offset=0x%lx creator_owner=%s typeinfo=%p rtti=%s owner=%s\n",
                     guid, (void *)factory_value, (void *)factory_vtable,
                     (void *)create_instance, (unsigned long)create_offset, create_owner,
                     (void *)creator_callback, (unsigned long)creator_offset, creator_owner,
                     (void *)typeinfo, rtti[0] ? rtti : "<unavailable>", owner);
    if (n > 0) {
        write(2, buf, (size_t)n);
    }
}

static const char *obj_name_for(void *ptr) {
    Dl_info info;
    if (ptr && dladdr(ptr, &info) != 0 && info.dli_fname && *info.dli_fname) {
        return info.dli_fname;
    }
    return "<unknown>";
}

static int is_probably_readable_string(const char *p) {
    if (!p) {
        return 0;
    }
    for (int i = 0; i < 80; ++i) {
        unsigned char c = (unsigned char)p[i];
        if (c == 0) {
            return i >= 3;
        }
        if (c < 0x20 || c > 0x7e) {
            return 0;
        }
    }
    return 1;
}

static void read_ascii(uintptr_t value, char *out, size_t out_size) {
    out[0] = 0;
    if (!readable_bytes(value, 80)) {
        return;
    }
    const char *p = (const char *)value;
    if (!is_probably_readable_string(p)) {
        return;
    }
    snprintf(out, out_size, "%.*s", 79, p);
}

static void guid_text(uintptr_t value, char *out, size_t out_size) {
    out[0] = 0;
    if (!readable_bytes(value, 16)) {
        return;
    }
    const uint8_t *p = (const uint8_t *)value;
    uint32_t a = *(const uint32_t *)(p + 0);
    uint16_t b = *(const uint16_t *)(p + 4);
    uint16_t c = *(const uint16_t *)(p + 6);
    snprintf(out, out_size,
             "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             a, b, c, p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
}

static int is_formdocument_guid(uintptr_t value) {
    if (!readable_bytes(value, 16)) {
        return 0;
    }
    const Guid *g = (const Guid *)value;
    return g->a == 0xa3f2959bu && g->b == 0x9763u && g->c == 0x43d6u &&
           g->d[0] == 0x90 && g->d[1] == 0x9d && g->d[2] == 0x1a &&
           g->d[3] == 0x37 && g->d[4] == 0xc1 && g->d[5] == 0x7d &&
           g->d[6] == 0x3d && g->d[7] == 0x48;
}

static void try_create_formdocument(uintptr_t class_guid, uintptr_t factory_value) {
    if (!create_formdocument || !is_formdocument_guid(class_guid)) {
        return;
    }
    void *factory = (void *)factory_value;
    uintptr_t vtable_value = 0;
    if (!readable_bytes(factory_value, sizeof(vtable_value))) {
        return;
    }
    memcpy(&vtable_value, factory, sizeof(vtable_value));
    if (!readable_bytes(vtable_value, sizeof(void *) * 4)) {
        return;
    }
    uintptr_t create_value = 0;
    memcpy(&create_value, (const void *)(vtable_value + sizeof(void *) * 3), sizeof(create_value));
    create_instance_fn create = (create_instance_fn)create_value;
    void *obj_unknown = 0;
    void *obj_document = 0;
    uintptr_t hr_unknown = create ? create(factory, 0, &IID_IUnknown, &obj_unknown) : 0xffffffffu;
    uintptr_t hr_document = create ? create(factory, 0, &IID_IFormDocument, &obj_document) : 0xffffffffu;

    char guid[64];
    char rtti[256];
    const char *owner = "<unknown>";
    uintptr_t typeinfo = 0;
    guid_text(class_guid, guid, sizeof(guid));
    object_rtti(obj_unknown, rtti, sizeof(rtti), &typeinfo, &owner);
    char document_rtti[256];
    const char *document_owner = "<unknown>";
    uintptr_t document_typeinfo = 0;
    object_rtti(obj_document, document_rtti, sizeof(document_rtti),
                &document_typeinfo, &document_owner);
    int type_ok = hr_unknown == 0 && hr_document == 0 &&
                  strcmp(rtti, "N4core11SCOM_ObjectI12FormDocumentEE") == 0 &&
                  strcmp(document_rtti, "N4core11SCOM_ObjectI12FormDocumentEE") == 0;
    char buf[1300];
    int n = snprintf(buf, sizeof(buf),
                     "OOF_CREATE_FORMDOCUMENT class_guid=%s factory=%p create=%p hr_unknown=0x%lx obj_unknown=%p hr_iformdocument=0x%lx obj_iformdocument=%p typeinfo=%p rtti=%s owner=%s iformdocument_typeinfo=%p iformdocument_rtti=%s iformdocument_owner=%s type_match=%s\n",
                     guid, factory, (void *)create, (unsigned long)hr_unknown, obj_unknown,
                     (unsigned long)hr_document, obj_document, (void *)typeinfo,
                     rtti[0] ? rtti : "<unavailable>", owner, (void *)document_typeinfo,
                     document_rtti[0] ? document_rtti : "<unavailable>", document_owner,
                     type_ok ? "yes" : "no");
    if (n > 0) {
        write(2, buf, (size_t)n);
    }
    if (obj_unknown) {
        uintptr_t unknown_vtable = 0;
        uintptr_t release_value = 0;
        if (readable_bytes((uintptr_t)obj_unknown, sizeof(unknown_vtable))) {
            memcpy(&unknown_vtable, obj_unknown, sizeof(unknown_vtable));
        }
        if (readable_bytes(unknown_vtable, sizeof(void *) * 3)) {
            memcpy(&release_value,
                   (const void *)(unknown_vtable + sizeof(void *) * 2),
                   sizeof(release_value));
        }
        release_fn release_unknown = (release_fn)release_value;
        if (release_unknown) {
            release_unknown(obj_unknown);
        }
    }
    if (obj_document) {
        uintptr_t document_vtable = 0;
        uintptr_t release_value = 0;
        if (readable_bytes((uintptr_t)obj_document, sizeof(document_vtable))) {
            memcpy(&document_vtable, obj_document, sizeof(document_vtable));
        }
        if (readable_bytes(document_vtable, sizeof(void *) * 3)) {
            memcpy(&release_value,
                   (const void *)(document_vtable + sizeof(void *) * 2),
                   sizeof(release_value));
        }
        release_fn release_document = (release_fn)release_value;
        if (release_document) {
            release_document(obj_document);
        }
    }
}

static uintptr_t hook_formdocument_create(void *factory, void *outer, const Guid *iid, void **out) {
    create_instance_fn original_create = (create_instance_fn)formdocument_factory_original_create_ptr;
    uintptr_t hr = original_create
                       ? original_create(factory, outer, iid, out)
                       : 0xffffffffu;
    char iid_buf[64];
    char buf[1400];
    guid_text((uintptr_t)iid, iid_buf, sizeof(iid_buf));
    void *obj = out ? *out : 0;
    char rtti[256];
    const char *owner = "<unknown>";
    uintptr_t typeinfo = 0;
    object_rtti(obj, rtti, sizeof(rtti), &typeinfo, &owner);
    int n = snprintf(buf, sizeof(buf),
                     "OOF_FORMDOCUMENT_FACTORY_CREATE factory=%p outer=%p iid=%s hr=0x%lx out=%p obj=%p typeinfo=%p rtti=%s owner=%s\n",
                     factory, outer, iid_buf, (unsigned long)hr, out, obj,
                     (void *)typeinfo, rtti[0] ? rtti : "<unavailable>", owner);
    if (n > 0) {
        write(2, buf, (size_t)n);
    }
    return hr;
}

static void install_formdocument_factory_proxy(uintptr_t class_guid, uintptr_t factory_value) {
    if (!proxy_formdocument_factory || !is_formdocument_guid(class_guid) || !factory_value) {
        return;
    }
    uintptr_t vtable_value = 0;
    if (!readable_bytes(factory_value, sizeof(vtable_value))) {
        return;
    }
    memcpy(&vtable_value, (const void *)factory_value, sizeof(vtable_value));
    if (vtable_value < sizeof(void *) * 2 ||
        !readable_bytes(vtable_value - sizeof(void *) * 2, sizeof(void *) * 7)) {
        return;
    }
    void **vptr = (void **)vtable_value;
    if (!formdocument_factory_original_vtable) {
        formdocument_factory_original_vtable = vptr;
        memcpy(formdocument_factory_vtable_proxy, vptr - 2, sizeof(formdocument_factory_vtable_proxy));
        memcpy(&formdocument_factory_original_create_ptr,
               (const void *)(vtable_value + sizeof(void *) * 3), sizeof(void *));
        formdocument_factory_vtable_proxy[5] = (void *)&hook_formdocument_create;
    }
    void **proxy_vtable = &formdocument_factory_vtable_proxy[2];
    memcpy((void *)factory_value, &proxy_vtable, sizeof(proxy_vtable));
    char buf[700];
    int n = snprintf(buf, sizeof(buf),
                     "OOF_FORMDOCUMENT_FACTORY_PROXY_INSTALLED factory=%p original_vtable=%p create=%p\n",
                     (void *)factory_value, vptr, formdocument_factory_original_create_ptr);
    if (n > 0) {
        write(2, buf, (size_t)n);
    }
}

static void log_method(const char *slot, void *self, uintptr_t a, uintptr_t b,
                       uintptr_t c, uintptr_t d, uintptr_t e) {
    char a_ascii[128];
    char b_ascii[128];
    char c_ascii[128];
    char a_guid[64];
    char b_guid[64];
    char c_guid[64];
    void *b_vptr = readable_bytes(b, sizeof(void *)) ? *(void **)b : 0;
    void *d_vptr = readable_bytes(d, sizeof(void *)) ? *(void **)d : 0;
    void *b_m0 = readable_bytes((uintptr_t)b_vptr, sizeof(void *) * 5) ? ((void **)b_vptr)[0] : 0;
    void *b_m1 = readable_bytes((uintptr_t)b_vptr, sizeof(void *) * 5) ? ((void **)b_vptr)[1] : 0;
    void *b_m2 = readable_bytes((uintptr_t)b_vptr, sizeof(void *) * 5) ? ((void **)b_vptr)[2] : 0;
    void *b_m3 = readable_bytes((uintptr_t)b_vptr, sizeof(void *) * 5) ? ((void **)b_vptr)[3] : 0;
    void *b_m4 = readable_bytes((uintptr_t)b_vptr, sizeof(void *) * 5) ? ((void **)b_vptr)[4] : 0;
    read_ascii(a, a_ascii, sizeof(a_ascii));
    read_ascii(b, b_ascii, sizeof(b_ascii));
    read_ascii(c, c_ascii, sizeof(c_ascii));
    guid_text(a, a_guid, sizeof(a_guid));
    guid_text(b, b_guid, sizeof(b_guid));
    guid_text(c, c_guid, sizeof(c_guid));

    char buf[1800];
    int n = snprintf(buf, sizeof(buf),
                     "OOF_REGISTRAR_%s self=%p a=0x%lx b=0x%lx c=0x%lx d=0x%lx e=0x%lx a_obj=%s b_obj=%s c_obj=%s d_obj=%s a_ascii=%s b_ascii=%s c_ascii=%s a_guid=%s b_guid=%s c_guid=%s b_vptr=%p b_vobj=%s b_m0=%p b_m0obj=%s b_m1=%p b_m2=%p b_m3=%p b_m3obj=%s b_m4=%p d_vptr=%p d_vobj=%s\n",
                     slot, self, (unsigned long)a, (unsigned long)b,
                     (unsigned long)c, (unsigned long)d, (unsigned long)e,
                     obj_name_for((void *)a), obj_name_for((void *)b),
                     obj_name_for((void *)c), obj_name_for((void *)d),
                     a_ascii, b_ascii, c_ascii, a_guid, b_guid, c_guid,
                     b_vptr, obj_name_for(b_vptr), b_m0, obj_name_for(b_m0),
                     b_m1, b_m2, b_m3, obj_name_for(b_m3), b_m4,
                     d_vptr, obj_name_for(d_vptr));
    if (n > 0) {
        write(2, buf, (size_t)n);
    }
}

static uintptr_t hook_060(void *self, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    if (inside_dsgnfrm) {
        log_method("060", self, a, b, c, d, e);
    }
    uintptr_t result = orig_060 ? orig_060(self, a, b, c, d, e) : 0;
    if (inside_dsgnfrm) {
        log_factory_rtti(a, b);
        install_formdocument_factory_proxy(a, b);
        try_create_formdocument(a, b);
    }
    return result;
}

static uintptr_t hook_070(void *self, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    if (inside_dsgnfrm) {
        log_method("070", self, a, b, c, d, e);
    }
    return orig_070 ? orig_070(self, a, b, c, d, e) : 0;
}

static uintptr_t hook_080(void *self, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    if (inside_dsgnfrm) {
        log_method("080", self, a, b, c, d, e);
    }
    return orig_080 ? orig_080(self, a, b, c, d, e) : 0;
}

static int module_is_dsgnfrm(void *module) {
    if (!module) {
        return 0;
    }
    void *vptr = *(void **)module;
    const char *obj = obj_name_for(vptr);
    return strstr(obj, "dsgnfrm.so") != 0;
}

void oof_register_all(void *self, void *process, void *registrar)
    __asm__("_ZN4core11SCOM_Module11registerAllEPNS_12SCOM_ProcessEPNS_19SCOM_ClassRegistrarE");

void oof_register_all(void *self, void *process, void *registrar) {
    register_all_fn fn = real_register_all();
    if (!fn) {
        return;
    }

    if (!module_is_dsgnfrm(self) || !registrar) {
        fn(self, process, registrar);
        return;
    }

    refresh_readable_ranges();

    void **saved = *(void ***)registrar;
    void *proxy[64];
    memcpy(proxy, saved, sizeof(proxy));

    orig_060 = (method6_fn)saved[0x60 / 8];
    orig_070 = (method6_fn)saved[0x70 / 8];
    orig_080 = (method6_fn)saved[0x80 / 8];
    proxy[0x60 / 8] = (void *)&hook_060;
    proxy[0x70 / 8] = (void *)&hook_070;
    proxy[0x80 / 8] = (void *)&hook_080;

    char buf[512];
    int n = snprintf(buf, sizeof(buf),
                     "OOF_REGISTRAR_PROXY_START module=%p process=%p registrar=%p registrar_vtable=%p\n",
                     self, process, registrar, saved);
    if (n > 0) {
        write(2, buf, (size_t)n);
    }

    *(void ***)registrar = proxy;
    inside_dsgnfrm = 1;
    fn(self, process, registrar);
    inside_dsgnfrm = 0;
    *(void ***)registrar = saved;

    n = snprintf(buf, sizeof(buf), "OOF_REGISTRAR_PROXY_DONE module=%p registrar=%p\n", self, registrar);
    if (n > 0) {
        write(2, buf, (size_t)n);
    }
}
