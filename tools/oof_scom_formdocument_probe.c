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

static void **orig_vtable = 0;
static method6_fn orig_060 = 0;
static method6_fn orig_070 = 0;
static method6_fn orig_080 = 0;
static int inside_dsgnfrm = 0;
static int create_formdocument = 0;
static int proxy_formdocument_factory = 0;
static int proxy_formdocument_object = 0;
static void *formdocument_factory_vtable_proxy[16];
static void **formdocument_factory_original_vtable = 0;
static void *formdocument_factory_original_create_ptr = 0;
static void *formdocument_object_vtable_proxy[96];
static void **formdocument_object_original_vtable = 0;
static unsigned long formdocument_method_call_count = 0;
static unsigned long formdocument_method_call_limit = 500;

typedef struct Guid {
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t d[8];
} Guid;

static const Guid IID_IUnknown = {0x00000000u, 0x0000u, 0x0000u, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const Guid IID_IFormDocument = {0x364f0971u, 0x70a0u, 0x47ddu, {0xaf, 0x6d, 0xb0, 0x94, 0xa7, 0xf6, 0x3a, 0xfb}};

typedef uintptr_t (*create_instance_fn)(void *, void *, const Guid *, void **);
typedef uintptr_t (*release_fn)(void *);

static register_all_fn real_register_all(void) {
    static register_all_fn fn = 0;
    if (!fn) {
        fn = (register_all_fn)dlsym(RTLD_NEXT, "_ZN4core11SCOM_Module11registerAllEPNS_12SCOM_ProcessEPNS_19SCOM_ClassRegistrarE");
    }
    return fn;
}

__attribute__((constructor))
static void load_readable_ranges(void) {
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
    const char *create_env = getenv("OOF_CREATE_FORMDOCUMENT");
    create_formdocument = create_env && create_env[0] && create_env[0] != '0';
    const char *proxy_env = getenv("OOF_PROXY_FORMDOCUMENT_FACTORY");
    proxy_formdocument_factory = proxy_env && proxy_env[0] && proxy_env[0] != '0';
    const char *object_proxy_env = getenv("OOF_PROXY_FORMDOCUMENT_OBJECT");
    proxy_formdocument_object = object_proxy_env && object_proxy_env[0] && object_proxy_env[0] != '0';
    const char *limit_env = getenv("OOF_FORMDOCUMENT_METHOD_LIMIT");
    if (limit_env && *limit_env) {
        formdocument_method_call_limit = strtoul(limit_env, 0, 10);
    }
}

static int readable_bytes(uintptr_t value, size_t size) {
    if (value < 0x10000 || size == 0) {
        return 0;
    }
    for (int i = 0; i < readable_range_count; ++i) {
        if (value >= readable_ranges[i].start && value + size <= readable_ranges[i].end) {
            return 1;
        }
    }
    return 0;
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
    snprintf(out, out_size, "%.*s", 120, p);
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
    return g->a == 0x0ec7b148u && g->b == 0xcdf9u && g->c == 0x451cu &&
           g->d[0] == 0x98 && g->d[1] == 0x21 && g->d[2] == 0x02 &&
           g->d[3] == 0x2b && g->d[4] == 0x95 && g->d[5] == 0xc0 &&
           g->d[6] == 0xfa && g->d[7] == 0x23;
}

static void try_create_formdocument(uintptr_t class_guid, uintptr_t factory_value) {
    if (!create_formdocument || !is_formdocument_guid(class_guid)) {
        return;
    }
    void *factory = (void *)factory_value;
    void *vptr = readable_bytes(factory_value, sizeof(void *)) ? *(void **)factory : 0;
    if (!readable_bytes((uintptr_t)vptr, sizeof(void *) * 4)) {
        return;
    }
    create_instance_fn create = (create_instance_fn)((void **)vptr)[3];
    release_fn release = (release_fn)((void **)vptr)[2];
    void *obj_unknown = 0;
    void *obj_form = 0;
    uintptr_t hr_unknown = create ? create(factory, 0, &IID_IUnknown, &obj_unknown) : 0xffffffffu;
    uintptr_t hr_form = create ? create(factory, 0, &IID_IFormDocument, &obj_form) : 0xffffffffu;

    char buf[1200];
    int n = snprintf(buf, sizeof(buf),
                     "OOF_CREATE_FORMDOCUMENT factory=%p create=%p hr_unknown=0x%lx obj_unknown=%p obj_unknown_vptr=%p obj_unknown_obj=%s hr_form=0x%lx obj_form=%p obj_form_vptr=%p obj_form_obj=%s\n",
                     factory, (void *)create, (unsigned long)hr_unknown, obj_unknown,
                     obj_unknown ? *(void **)obj_unknown : 0,
                     obj_unknown ? obj_name_for(*(void **)obj_unknown) : "<null>",
                     (unsigned long)hr_form, obj_form,
                     obj_form ? *(void **)obj_form : 0,
                     obj_form ? obj_name_for(*(void **)obj_form) : "<null>");
    if (n > 0) {
        write(2, buf, (size_t)n);
    }
    if (obj_unknown && release) {
        release(obj_unknown);
    }
    if (obj_form && obj_form != obj_unknown && release) {
        release(obj_form);
    }
}

static uintptr_t formdocument_method_hook(unsigned int slot, void *self, uintptr_t a,
                                          uintptr_t b, uintptr_t c, uintptr_t d,
                                          uintptr_t e) {
    if (formdocument_method_call_count < formdocument_method_call_limit) {
        ++formdocument_method_call_count;
        char a_guid[64];
        char b_guid[64];
        char a_ascii[96];
        char b_ascii[96];
        guid_text(a, a_guid, sizeof(a_guid));
        guid_text(b, b_guid, sizeof(b_guid));
        read_ascii(a, a_ascii, sizeof(a_ascii));
        read_ascii(b, b_ascii, sizeof(b_ascii));
        char buf[1500];
        int n = snprintf(buf, sizeof(buf),
                         "OOF_FORMDOCUMENT_METHOD slot=%u self=%p a=0x%lx b=0x%lx c=0x%lx d=0x%lx e=0x%lx a_obj=%s b_obj=%s c_obj=%s a_guid=%s b_guid=%s a_ascii=%s b_ascii=%s\n",
                         slot, self, (unsigned long)a, (unsigned long)b,
                         (unsigned long)c, (unsigned long)d, (unsigned long)e,
                         obj_name_for((void *)a), obj_name_for((void *)b),
                         obj_name_for((void *)c), a_guid, b_guid, a_ascii, b_ascii);
        if (n > 0) {
            write(2, buf, (size_t)n);
        }
    }
    method6_fn original = formdocument_object_original_vtable
                              ? (method6_fn)formdocument_object_original_vtable[slot]
                              : 0;
    return original ? original(self, a, b, c, d, e) : 0;
}

#define DECL_FD_HOOK(N) \
static uintptr_t fd_hook_##N(void *self, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) { \
    return formdocument_method_hook((N), self, a, b, c, d, e); \
}

DECL_FD_HOOK(0) DECL_FD_HOOK(1) DECL_FD_HOOK(2) DECL_FD_HOOK(3)
DECL_FD_HOOK(4) DECL_FD_HOOK(5) DECL_FD_HOOK(6) DECL_FD_HOOK(7)
DECL_FD_HOOK(8) DECL_FD_HOOK(9) DECL_FD_HOOK(10) DECL_FD_HOOK(11)
DECL_FD_HOOK(12) DECL_FD_HOOK(13) DECL_FD_HOOK(14) DECL_FD_HOOK(15)
DECL_FD_HOOK(16) DECL_FD_HOOK(17) DECL_FD_HOOK(18) DECL_FD_HOOK(19)
DECL_FD_HOOK(20) DECL_FD_HOOK(21) DECL_FD_HOOK(22) DECL_FD_HOOK(23)
DECL_FD_HOOK(24) DECL_FD_HOOK(25) DECL_FD_HOOK(26) DECL_FD_HOOK(27)
DECL_FD_HOOK(28) DECL_FD_HOOK(29) DECL_FD_HOOK(30) DECL_FD_HOOK(31)
DECL_FD_HOOK(32) DECL_FD_HOOK(33) DECL_FD_HOOK(34) DECL_FD_HOOK(35)
DECL_FD_HOOK(36) DECL_FD_HOOK(37) DECL_FD_HOOK(38) DECL_FD_HOOK(39)
DECL_FD_HOOK(40) DECL_FD_HOOK(41) DECL_FD_HOOK(42) DECL_FD_HOOK(43)
DECL_FD_HOOK(44) DECL_FD_HOOK(45) DECL_FD_HOOK(46) DECL_FD_HOOK(47)
DECL_FD_HOOK(48) DECL_FD_HOOK(49) DECL_FD_HOOK(50) DECL_FD_HOOK(51)
DECL_FD_HOOK(52) DECL_FD_HOOK(53) DECL_FD_HOOK(54) DECL_FD_HOOK(55)
DECL_FD_HOOK(56) DECL_FD_HOOK(57) DECL_FD_HOOK(58) DECL_FD_HOOK(59)
DECL_FD_HOOK(60) DECL_FD_HOOK(61) DECL_FD_HOOK(62) DECL_FD_HOOK(63)

static void install_formdocument_object_proxy(void *obj) {
    if (!proxy_formdocument_object || !obj) {
        return;
    }
    void **vptr = *(void ***)obj;
    if (!readable_bytes((uintptr_t)vptr, sizeof(void *) * 64)) {
        return;
    }
    if (!formdocument_object_original_vtable) {
        formdocument_object_original_vtable = vptr;
        memcpy(formdocument_object_vtable_proxy, vptr, sizeof(formdocument_object_vtable_proxy));
        void *hooks[64] = {
            fd_hook_0, fd_hook_1, fd_hook_2, fd_hook_3, fd_hook_4, fd_hook_5, fd_hook_6, fd_hook_7,
            fd_hook_8, fd_hook_9, fd_hook_10, fd_hook_11, fd_hook_12, fd_hook_13, fd_hook_14, fd_hook_15,
            fd_hook_16, fd_hook_17, fd_hook_18, fd_hook_19, fd_hook_20, fd_hook_21, fd_hook_22, fd_hook_23,
            fd_hook_24, fd_hook_25, fd_hook_26, fd_hook_27, fd_hook_28, fd_hook_29, fd_hook_30, fd_hook_31,
            fd_hook_32, fd_hook_33, fd_hook_34, fd_hook_35, fd_hook_36, fd_hook_37, fd_hook_38, fd_hook_39,
            fd_hook_40, fd_hook_41, fd_hook_42, fd_hook_43, fd_hook_44, fd_hook_45, fd_hook_46, fd_hook_47,
            fd_hook_48, fd_hook_49, fd_hook_50, fd_hook_51, fd_hook_52, fd_hook_53, fd_hook_54, fd_hook_55,
            fd_hook_56, fd_hook_57, fd_hook_58, fd_hook_59, fd_hook_60, fd_hook_61, fd_hook_62, fd_hook_63,
        };
        for (int i = 0; i < 64; ++i) {
            formdocument_object_vtable_proxy[i] = hooks[i];
        }
    }
    *(void ***)obj = formdocument_object_vtable_proxy;
    char buf[700];
    int n = snprintf(buf, sizeof(buf),
                     "OOF_FORMDOCUMENT_OBJECT_PROXY_INSTALLED obj=%p original_vtable=%p original_obj=%s\n",
                     obj, vptr, obj_name_for(vptr));
    if (n > 0) {
        write(2, buf, (size_t)n);
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
    int n = snprintf(buf, sizeof(buf),
                     "OOF_FORMDOCUMENT_FACTORY_CREATE factory=%p outer=%p iid=%s hr=0x%lx out=%p obj=%p obj_vptr=%p obj_vobj=%s\n",
                     factory, outer, iid_buf, (unsigned long)hr, out, obj,
                     obj ? *(void **)obj : 0,
                     obj ? obj_name_for(*(void **)obj) : "<null>");
    if (n > 0) {
        write(2, buf, (size_t)n);
    }
    install_formdocument_object_proxy(obj);
    return hr;
}

static void install_formdocument_factory_proxy(uintptr_t class_guid, uintptr_t factory_value) {
    if (!proxy_formdocument_factory || !is_formdocument_guid(class_guid) || !factory_value) {
        return;
    }
    void **vptr = readable_bytes(factory_value, sizeof(void *)) ? *(void ***)factory_value : 0;
    if (!readable_bytes((uintptr_t)vptr, sizeof(void *) * 8)) {
        return;
    }
    if (!formdocument_factory_original_vtable) {
        formdocument_factory_original_vtable = vptr;
        memcpy(formdocument_factory_vtable_proxy, vptr, sizeof(formdocument_factory_vtable_proxy));
        formdocument_factory_original_create_ptr = vptr[3];
        formdocument_factory_vtable_proxy[3] = (void *)&hook_formdocument_create;
    }
    *(void ***)factory_value = formdocument_factory_vtable_proxy;
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

    load_readable_ranges();

    void **saved = *(void ***)registrar;
    void *proxy[64];
    memcpy(proxy, saved, sizeof(proxy));

    orig_vtable = saved;
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
