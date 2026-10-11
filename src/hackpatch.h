#ifndef HACKPATCH_H
#define HACKPATCH_H

#include "patchengine.h"

#define HACKPATCH_REVISION 75
#define HACKPATCH_SERIALIZED_SIZE 888

int hackpatch_serialize(const t_patch *patch, unsigned romsize, const uint8_t hash[32], uint8_t *buffer);
bool hackpatch_unserialize(const uint8_t *buffer, unsigned size, unsigned romsize,
                           const uint8_t hash[32], t_patch *patch);
bool hackpatch_hash_rom(const char *filename, unsigned romsize, uint8_t hash[32],
                        void (*progress)(unsigned, unsigned));
bool hackpatch_load(const char *filename, unsigned romsize, const uint8_t hash[32], t_patch *patch);
bool hackpatch_load_cached(const char *filename, unsigned romsize, t_patch *patch);
bool hackpatch_quick_id(const char *filename, const uint8_t *header, const uint32_t *offsets,
                        unsigned count, uint32_t *identity);
bool hackpatch_db_samples(const uint8_t *database, unsigned romsize, uint32_t header_identity,
                          uint32_t *offsets, unsigned *count);
bool hackpatch_db_lookup(const uint8_t *database, unsigned romsize, uint32_t identity, t_patch *patch);
bool hackpatch_load_external_database(void);
bool hackpatch_save(const char *filename, unsigned romsize, const uint8_t hash[32], const t_patch *patch);
bool hackpatch_apply_save_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch);

#endif
