#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <execinfo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef uintptr_t (*scom_main3_fn)(uintptr_t, uintptr_t, uintptr_t);
typedef int (*dllmain_fn)(void *, int, void *);

typedef struct Module {
    const char *name;
    void *handle;
    scom_main3_fn scom_main;
} Module;

static void *fake_registrar_vtable[64];
static void *fake_process_vtable[96];
static void *fake_service_vtable[16];
static void *fake_registrar[4];
static unsigned char fake_process[4096];
static void *fake_service[4];
static const char *active_module_name = "";
static const char *active_process_identity = "";

void *oof_core_current_process(void) __asm__("_ZN4core15current_processEv");
void *oof_core_current_process(void) {
    fprintf(stderr, "OOF_SCOM_HOST_CURRENT_PROCESS process=%p\n", fake_process);
    return fake_process;
}

static void *find_scom_module_object(void *scom_main) {
    unsigned char *p = (unsigned char *)scom_main;
    for (size_t i = 0; i + 7 < 256; i++) {
        if (p[i] == 0x48 && p[i + 1] == 0x8d && p[i + 2] == 0x3d) {
            int32_t rel = 0;
            memcpy(&rel, p + i + 3, sizeof(rel));
            return p + i + 7 + rel;
        }
    }
    return NULL;
}

static void guid_text(uintptr_t value, char *out, size_t out_size) {
    const unsigned char *p = (const unsigned char *)value;
    if (!value) {
        snprintf(out, out_size, "<null>");
        return;
    }
    snprintf(out, out_size,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             p[3], p[2], p[1], p[0], p[5], p[4], p[7], p[6],
             p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
}

static uintptr_t fake_registrar_method(const char *slot, void *self, uintptr_t a,
                                       uintptr_t b, uintptr_t c, uintptr_t d,
                                       uintptr_t e) {
    char guid[80];
    guid_text(a, guid, sizeof(guid));
    fprintf(stderr,
            "OOF_SCOM_HOST_FAKE_REGISTRAR_%s self=%p class_guid=%s a=0x%lx b=0x%lx c=0x%lx d=0x%lx e=0x%lx\n",
            slot, self, guid, (unsigned long)a, (unsigned long)b,
            (unsigned long)c, (unsigned long)d, (unsigned long)e);
    return 0;
}

static uintptr_t fake_registrar_060(void *self, uintptr_t a, uintptr_t b,
                                    uintptr_t c, uintptr_t d, uintptr_t e) {
    return fake_registrar_method("060", self, a, b, c, d, e);
}

static uintptr_t fake_registrar_070(void *self, uintptr_t a, uintptr_t b,
                                    uintptr_t c, uintptr_t d, uintptr_t e) {
    return fake_registrar_method("070", self, a, b, c, d, e);
}

static uintptr_t fake_registrar_080(void *self, uintptr_t a, uintptr_t b,
                                    uintptr_t c, uintptr_t d, uintptr_t e) {
    return fake_registrar_method("080", self, a, b, c, d, e);
}

static uintptr_t fake_registrar_default(void *self, uintptr_t a, uintptr_t b,
                                        uintptr_t c, uintptr_t d, uintptr_t e) {
    return fake_registrar_method("DEFAULT", self, a, b, c, d, e);
}

static uintptr_t fake_process_string_method(void *out, uintptr_t a, uintptr_t b,
                                            uintptr_t c, uintptr_t d, uintptr_t e) {
    memset(out, 0, 32);
    const char *override_name = getenv("OOF_SCOM_HOST_FAKE_PROCESS_NAME");
    const char *value = override_name && *override_name
                            ? override_name
                            : (active_process_identity && *active_process_identity
                                   ? active_process_identity
                                   : (active_module_name && *active_module_name ? active_module_name : ""));
    size_t len = strlen(value);
    if (len > 23) {
        len = 23;
    }
    memcpy(out, value, len);
    ((char *)out)[len] = 0;
    ((unsigned char *)out)[0x17] = (unsigned char)(23 - len);
    fprintf(stderr,
            "OOF_SCOM_HOST_FAKE_PROCESS_STRING out=%p value=%s a=0x%lx b=0x%lx c=0x%lx d=0x%lx e=0x%lx\n",
            out, value,
            (unsigned long)a, (unsigned long)b, (unsigned long)c,
            (unsigned long)d, (unsigned long)e);
    return 0;
}

static uintptr_t fake_process_method(void *self, uintptr_t a, uintptr_t b,
                                     uintptr_t c, uintptr_t d, uintptr_t e) {
    fprintf(stderr,
            "OOF_SCOM_HOST_FAKE_PROCESS_METHOD self=%p a=0x%lx b=0x%lx c=0x%lx d=0x%lx e=0x%lx\n",
            self, (unsigned long)a, (unsigned long)b, (unsigned long)c,
            (unsigned long)d, (unsigned long)e);
    return 0;
}

static uintptr_t fake_process_get_object_method(void *self, uintptr_t a, uintptr_t b,
                                                uintptr_t c, uintptr_t d, uintptr_t e) {
    if (c) {
        *(uintptr_t *)c = (uintptr_t)fake_service;
    }
    fprintf(stderr,
            "OOF_SCOM_HOST_FAKE_PROCESS_GET_OBJECT self=%p a=0x%lx b=0x%lx out=0x%lx d=0x%lx e=0x%lx service=%p\n",
            self, (unsigned long)a, (unsigned long)b, (unsigned long)c,
            (unsigned long)d, (unsigned long)e, fake_service);
    return 1;
}

static uintptr_t fake_process_lookup_method(void *self, uintptr_t a, uintptr_t b,
                                            uintptr_t c, uintptr_t d, uintptr_t e) {
    fprintf(stderr,
            "OOF_SCOM_HOST_FAKE_PROCESS_LOOKUP self=%p key=0x%lx b=0x%lx c=0x%lx d=0x%lx e=0x%lx service=%p\n",
            self, (unsigned long)a, (unsigned long)b, (unsigned long)c,
            (unsigned long)d, (unsigned long)e, fake_service);
    return (uintptr_t)fake_service;
}

static uintptr_t fake_service_method(void *self, uintptr_t a, uintptr_t b,
                                     uintptr_t c, uintptr_t d, uintptr_t e) {
    fprintf(stderr,
            "OOF_SCOM_HOST_FAKE_SERVICE_METHOD self=%p a=0x%lx b=0x%lx c=0x%lx d=0x%lx e=0x%lx\n",
            self, (unsigned long)a, (unsigned long)b, (unsigned long)c,
            (unsigned long)d, (unsigned long)e);
    return 0;
}

static uintptr_t fake_service_acquire_method(void *self, uintptr_t a, uintptr_t b,
                                             uintptr_t c, uintptr_t d, uintptr_t e) {
    fprintf(stderr,
            "OOF_SCOM_HOST_FAKE_SERVICE_ACQUIRE self=%p a=0x%lx b=0x%lx c=0x%lx d=0x%lx e=0x%lx\n",
            self, (unsigned long)a, (unsigned long)b, (unsigned long)c,
            (unsigned long)d, (unsigned long)e);
    return (uintptr_t)self;
}

static void setup_fake_registrar(uintptr_t *process, uintptr_t *registrar) {
    for (size_t i = 0; i < sizeof(fake_process_vtable) / sizeof(fake_process_vtable[0]); ++i) {
        fake_process_vtable[i] = (void *)&fake_process_method;
    }
    fake_process_vtable[0x170 / sizeof(void *)] = (void *)&fake_process_string_method;
    fake_process_vtable[0x178 / sizeof(void *)] = (void *)&fake_process_string_method;
    fake_process_vtable[0x20 / sizeof(void *)] = (void *)&fake_process_get_object_method;
    fake_process_vtable[0x70 / sizeof(void *)] = (void *)&fake_process_lookup_method;
    fake_process_vtable[0x168 / sizeof(void *)] = (void *)&fake_process_method;
    fake_process_vtable[0x140 / sizeof(void *)] = (void *)&fake_process_method;
    *(void **)fake_process = fake_process_vtable;

    for (size_t i = 0; i < sizeof(fake_service_vtable) / sizeof(fake_service_vtable[0]); ++i) {
        fake_service_vtable[i] = (void *)&fake_service_method;
    }
    fake_service_vtable[0x10 / sizeof(void *)] = (void *)&fake_service_method;
    fake_service_vtable[0x18 / sizeof(void *)] = (void *)&fake_service_acquire_method;
    fake_service_vtable[0x48 / sizeof(void *)] = (void *)&fake_service_method;
    fake_service[0] = fake_service_vtable;

    for (size_t i = 0; i < sizeof(fake_registrar_vtable) / sizeof(fake_registrar_vtable[0]); ++i) {
        fake_registrar_vtable[i] = (void *)&fake_registrar_default;
    }
    fake_registrar_vtable[0x60 / sizeof(void *)] = (void *)&fake_registrar_060;
    fake_registrar_vtable[0x70 / sizeof(void *)] = (void *)&fake_registrar_070;
    fake_registrar_vtable[0x80 / sizeof(void *)] = (void *)&fake_registrar_080;
    fake_registrar[0] = fake_registrar_vtable;
    if (!*process) {
        *process = (uintptr_t)fake_process;
    }
    if (!*registrar) {
        *registrar = (uintptr_t)fake_registrar;
    }
    fprintf(stderr, "OOF_SCOM_HOST_FAKE_REGISTRAR_READY process=0x%lx registrar=0x%lx vtable=%p\n",
            (unsigned long)*process, (unsigned long)*registrar, (void *)fake_registrar_vtable);
}

static Module default_modules[] = {
    {"core85.so", 0, 0},
    {"coreui85.so", 0, 0},
    {"uiproxywx.so", 0, 0},
    {"wbase.so", 0, 0},
    {"frmcore.so", 0, 0},
    {"dsgnfrm.so", 0, 0},
};
static Module extra_modules[] = {
    {"html.so", 0, 0},
    {"moxel.so", 0, 0},
    {"chart.so", 0, 0},
    {"mngcore.so", 0, 0},
    {"mngbase.so", 0, 0},
    {"mngui.so", 0, 0},
};

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s <platform-dir> [mode] [process-hex] [registrar-hex]\n"
            "Loads 1C platform libraries and probes SCOM_Main(mode, process, registrar).\n",
            argv0);
}

static uintptr_t parse_word(const char *text) {
    if (!text || !*text) {
        return 0;
    }
    return (uintptr_t)strtoull(text, 0, 0);
}

static void load_module(const char *platform_dir, Module *module) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", platform_dir, module->name);
    dlerror();
    module->handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!module->handle) {
        fprintf(stderr, "OOF_SCOM_HOST_DLOPEN_FAIL module=%s error=%s\n",
                module->name, dlerror());
        return;
    }
    module->scom_main = (scom_main3_fn)dlsym(module->handle, "SCOM_Main");
    fprintf(stderr, "OOF_SCOM_HOST_DLOPEN_OK module=%s handle=%p scom_main=%p\n",
            module->name, module->handle, (void *)module->scom_main);
    dllmain_fn dllmain = (dllmain_fn)dlsym(module->handle, "DllMain");
    const char *call_dllmain = getenv("OOF_SCOM_HOST_CALL_DLLMAIN");
    if (dllmain && (!call_dllmain || strcmp(call_dllmain, "0") != 0)) {
        int rc = dllmain(module->handle, 1, 0);
        fprintf(stderr, "OOF_SCOM_HOST_DLLMAIN module=%s rc=%d\n", module->name, rc);
    }
}

static void crash_handler(int sig) {
    void *frames[64];
    int n = backtrace(frames, 64);
    fprintf(stderr, "OOF_SCOM_HOST_CRASH signal=%d frames=%d\n", sig, n);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    _exit(128 + sig);
}

static int module_enabled(const Module *module) {
    const char *only = getenv("OOF_SCOM_HOST_ONLY");
    if (!only || !*only) {
        return 1;
    }
    return strstr(only, module->name) != 0;
}

static int run_scom_child(Module *module, uintptr_t mode, uintptr_t process, uintptr_t registrar) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 125;
    }
    if (pid == 0) {
        signal(SIGSEGV, crash_handler);
        signal(SIGBUS, crash_handler);
        signal(SIGABRT, crash_handler);
        active_module_name = module->name;
        void *module_object = find_scom_module_object((void *)module->scom_main);
        active_process_identity = "";
        if (module_object) {
            const char *identity = *(const char **)((unsigned char *)module_object + 0x10);
            const char *version = *(const char **)((unsigned char *)module_object + 0x08);
            const void *classes = *(const void **)((unsigned char *)module_object + 0x18);
            const void *bundle = *(const void **)((unsigned char *)module_object + 0x20);
            active_process_identity = identity ? identity : "";
            fprintf(stderr,
                    "OOF_SCOM_HOST_MODULE_OBJECT module=%s object=%p version=%s identity=%s classes=%p bundle=%p\n",
                    module->name, module_object, version ? version : "",
                    active_process_identity, classes, bundle);
        } else {
            fprintf(stderr,
                    "OOF_SCOM_HOST_MODULE_OBJECT_MISSING module=%s scom_main=%p\n",
                    module->name, (void *)module->scom_main);
        }
        uintptr_t rc = module->scom_main(process, registrar, mode);
        fprintf(stderr,
                "OOF_SCOM_HOST_CALL_RETURN module=%s mode=0x%lx process=0x%lx registrar=0x%lx rc=0x%lx\n",
                module->name, (unsigned long)mode, (unsigned long)process,
                (unsigned long)registrar, (unsigned long)rc);
        _exit(0);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 125;
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "OOF_SCOM_HOST_CALL_SIGNAL module=%s mode=0x%lx signal=%d\n",
                module->name, (unsigned long)mode, WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    fprintf(stderr, "OOF_SCOM_HOST_CALL_EXIT module=%s mode=0x%lx code=%d\n",
            module->name, (unsigned long)mode, WEXITSTATUS(status));
    return WEXITSTATUS(status);
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 5) {
        usage(argv[0]);
        return 2;
    }
    const char *platform_dir = argv[1];
    uintptr_t mode = argc >= 3 ? parse_word(argv[2]) : 0;
    uintptr_t process = argc >= 4 ? parse_word(argv[3]) : 0;
    uintptr_t registrar = argc >= 5 ? parse_word(argv[4]) : 0;
    const char *fake_registrar_env = getenv("OOF_SCOM_HOST_FAKE_REGISTRAR");
    if (fake_registrar_env && strcmp(fake_registrar_env, "0") != 0) {
        setup_fake_registrar(&process, &registrar);
    }

    setenv("LD_LIBRARY_PATH", platform_dir, 0);
    fprintf(stderr, "OOF_SCOM_HOST_START platform=%s mode=0x%lx process=0x%lx registrar=0x%lx\n",
            platform_dir, (unsigned long)mode, (unsigned long)process, (unsigned long)registrar);

    size_t count = sizeof(default_modules) / sizeof(default_modules[0]);
    for (size_t i = 0; i < count; ++i) {
        if (!module_enabled(&default_modules[i])) {
            continue;
        }
        load_module(platform_dir, &default_modules[i]);
    }
    const char *load_extra = getenv("OOF_SCOM_HOST_LOAD_EXTRA");
    if (load_extra && strcmp(load_extra, "0") != 0) {
        size_t extra_count = sizeof(extra_modules) / sizeof(extra_modules[0]);
        for (size_t i = 0; i < extra_count; ++i) {
            if (!module_enabled(&extra_modules[i])) {
                continue;
            }
            load_module(platform_dir, &extra_modules[i]);
        }
    }

    int saw_scom = 0;
    int last = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!module_enabled(&default_modules[i]) || !default_modules[i].scom_main) {
            continue;
        }
        saw_scom = 1;
        last = run_scom_child(&default_modules[i], mode, process, registrar);
    }
    if (!saw_scom) {
        fprintf(stderr, "OOF_SCOM_HOST_NO_SCOM_MAIN\n");
        return 1;
    }
    return last == 0 ? 0 : 1;
}
