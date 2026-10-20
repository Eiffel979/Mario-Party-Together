/* Byte order for msm's disc structures (P1-15, before M1).
 *
 * msm reads its sound-data file (.msm) and its stream file (.pdt) straight from disc into these
 * structures (include/game/msm_data.h) and uses them as host integers. The files are big-endian. Each
 * function converts one structure, or an array of them, in place right after the read (patch 0015).
 * They are written from the structure definitions alone. Everything that msm only passes on to MusyX
 * (project, pool, sample directory, songs and sample data) stays big-endian, because MusyX's PC loader
 * reads it as big-endian itself. Each function converts at most `size` bytes and ignores a trailing
 * partial record.
 */
#ifndef MPT_MSM_SWAP_H
#define MPT_MSM_SWAP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct MSMHeader_s;
struct MSMInfo_s;
struct MSMGrpHead_s;
struct MSMStreamHeader_s;

void mpt_msm_swap_header(struct MSMHeader_s* header);          /* 0x60 bytes at offset 0 */
void mpt_msm_swap_info(struct MSMInfo_s* info, size_t size);   /* infoSize bytes at infoOfs */
void mpt_msm_swap_aux_params(void* params, size_t size);       /* MSM_AUXPARAM[] */
void mpt_msm_swap_grp_info(void* infos, size_t size);          /* MSM_GRP_INFO[] */
void mpt_msm_swap_grp_head(struct MSMGrpHead_s* head);          /* the start of each group's data */
void mpt_msm_swap_mus(void* mus, size_t size);                 /* MSM_MUS[] */
void mpt_msm_swap_se(void* se, size_t size);                   /* MSM_SE[] */
void mpt_msm_swap_stream_header(struct MSMStreamHeader_s* header); /* 0x20 bytes of the .pdt */

/* The .pdt tables, after the header is converted. `list` holds `list_size` bytes from
 * streamPackListOfs: one u32 file offset per stream, 0 for none. `packs` holds `packs_size` bytes
 * from streamPackOfs, and each pack is converted once however many streams share it; a pack's second
 * stream entry is converted only for stereo packs. `adpcm` holds SND_ADPCMSTREAM_INFO records. */
void mpt_msm_swap_stream_tables(const struct MSMStreamHeader_s* header, unsigned int* list, size_t list_size,
                                void* packs, size_t packs_size, void* adpcm, size_t adpcm_size);

#ifdef __cplusplus
}
#endif

#endif /* MPT_MSM_SWAP_H */
