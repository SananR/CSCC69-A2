
/* States in a thread's life cycle. */
enum virtual_memory_type
  {
    FILE_PAGE,
    SWAP_PAGE
  };

struct virtual_memory_entry
  {
      uint8_t *uaddr;                       /* User virtual address of page */
  		struct hash_elem hash_elem;           /* Hash table element. */
  		unsigned vpn;                         /* Virtual page number */
      virtual_memory_type *page_type;       /* The virtual memory type, either file page or a swap page */

      struct file *file;                    /* Reference to the user file */
      uint32_t read_bytes;                  /* Number of read bytes for loading the file */
      uint32_t zero_bytes;                  /* Number of zeroed bytes for loading the file */
      off_t ofs;                            /* Offset for reading the file */

      bool writable;                        /* Whether this frame can be written to */
      bool in_memory;                       /* Whether this frame is in memory */
  };


bool virtual_memory_entry_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
unsigned virtual_memory_entry_hash (const struct hash_elem *p_, void *aux UNUSED);
