#ifndef MODULE_SYMBOL_H
#define MODULE_SYMBOL_H

struct kmod_symbol {
  const char *name;
  void *addr;
};

#define KMOD_EXPORT(name) \
  static struct kmod_symbol __ksym_##name \
    __attribute__((used, section(".ksyms"))) = { #name, (void*)&name }

#endif
