// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

typedef long long int64;

// File header
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12];
  ushort type;
  ushort machine;
  uint version;
  uint64 entry;
  uint64 phoff;
  uint64 shoff;
  uint flags;
  ushort ehsize;
  ushort phentsize;
  ushort phnum;
  ushort shentsize;
  ushort shnum;
  ushort shstrndx;
};

// Program section header
struct proghdr {
  uint32 type;
  uint32 flags;
  uint64 off;
  uint64 vaddr;
  uint64 paddr;
  uint64 filesz;
  uint64 memsz;
  uint64 align;
};

// Section header (ELF64)
struct shdr {
  uint32 sh_name;
  uint32 sh_type;
  uint64 sh_flags;
  uint64 sh_addr;
  uint64 sh_offset;
  uint64 sh_size;
  uint32 sh_link;
  uint32 sh_info;
  uint64 sh_addralign;
  uint64 sh_entsize;
};

// Symbol table entry (ELF64)
struct sym {
  uint32 st_name;
  uchar st_info;
  uchar st_other;
  ushort st_shndx;
  uint64 st_value;
  uint64 st_size;
};

// Relocation entry with explicit addend (RELA)
struct rela {
  uint64 r_offset;
  uint64 r_info;
  int64 r_addend;
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4

#define SHT_SYMTAB 2
#define SHT_RELA   4

#define R_RISCV_32        1
#define R_RISCV_64        2
#define R_RISCV_RELATIVE  3
#define R_RISCV_PCREL_HI20  23
#define R_RISCV_PCREL_LO12_I 24
#define R_RISCV_PCREL_LO12_S 25
#define R_RISCV_RELAX     51
