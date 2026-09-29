/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_GFX_REGISTERS_H
#define MT_GUEST_GFX_REGISTERS_H
#include "mt_device_profile.h"

#define MT_GFX_VERTEX_REG_BYTES 0xb0U
#define MT_GFX_FRAGMENT_REG_BYTES 0x160U
#define MT_GFX_REG_BYTES (MT_GFX_VERTEX_REG_BYTES + MT_GFX_FRAGMENT_REG_BYTES)
/* CPU-side state records consumed by mtdxum64 158f90/159520, family 2.
 * These are packed register values, not user pointers. The field names remain
 * neutral until their producer/resource semantics are traced. No ioctl uses
 * this format. A successful pack does not validate GPU addresses or a draw. */
struct mt_gfx_register_source {
 u8 metadata[0xe8], vertex[0x78], render[0x400], context[0x40];
 u64 render_base;
};
static inline void mt_gfx_copy_word(u8 *out, u32 dest, const u8 *source, u32 off, u32 n)
{
 memcpy(out + dest, source + off, n);
}
static inline int mt_gfx_registers_encode(void *out, u32 capacity,
 const struct mt_device_profile *profile, const struct mt_gfx_register_source *source)
{
 static const u32 meta_offsets[8][2] = {
  {0x00,0x08},{0x08,0x18},{0x10,0x38},{0x18,0x48},
  {0x30,0xb0},{0x40,0x68},{0x48,0x70},{0x50,0x58}};
 static const u32 vertex_qwords[5][2] = {
  {0x20,0x08},{0x28,0x00},{0x38,0x10},
  {0x58,0x70},{0x60,0x58}};
 static const u32 vertex_dwords[9] = {0x20,0x28,0x30,0x18,0x48,0x40,0x50,0x60,0x68};
 static const u32 render_dwords[17] = {0x1f8,0x1d8,0x1e0,0x180,0x210,0x150,
  0x158,0x160,0x168,0x170,0x178,0x208,0x370,0x378,0x380,0x1c8,0x2e8};
 u8 *v=out,*f;
 u32 targets,i;
 if (!out || capacity < MT_GFX_REG_BYTES || !source)
  return -EINVAL;
 if (!profile || profile->family != 2 || profile->primary_version != 2)
  return -EOPNOTSUPP;
 /* Callers use separate workspaces; reject overlap before modifying output. */
 if ((unsigned long)out < (unsigned long)source + sizeof(*source) &&
     (unsigned long)source < (unsigned long)out + MT_GFX_REG_BYTES)
  return -EINVAL;
 memcpy(&targets,source->render+0x148,4);
 if (targets > 8)
  return -E2BIG;
 memset(out,0,MT_GFX_REG_BYTES);
 for (i=0;i<8;i++) mt_gfx_copy_word(v,meta_offsets[i][0],source->metadata,meta_offsets[i][1],8);
 for (i=0;i<5;i++) mt_gfx_copy_word(v,vertex_qwords[i][0],source->vertex,vertex_qwords[i][1],8);
 for (i=0;i<9;i++) mt_gfx_copy_word(v,0x68+i*4,source->vertex,vertex_dwords[i],4);
 f=v+MT_GFX_VERTEX_REG_BYTES;
 mt_gfx_copy_word(f,0x00,source->metadata,8,8);
 mt_gfx_copy_word(f,0x08,source->context,0x28,8);
 for (i=0;i<targets;i++) mt_gfx_copy_word(f,0x10+i*24,source->render,8+i*40,24);
 mt_gfx_copy_word(f,0xd0,source->render,0x388,8);
 memcpy(f+0xd8,&source->render_base,8);
 mt_gfx_copy_word(f,0xe0,source->metadata,0x18,8);
 mt_gfx_copy_word(f,0xe8,source->metadata,0x68,8);
 mt_gfx_copy_word(f,0xf0,source->metadata,0x70,8);
 mt_gfx_copy_word(f,0xf8,source->render,0x1e8,8);
 mt_gfx_copy_word(f,0x100,source->render,0x1f0,8);
 for (i=0;i<17;i++) mt_gfx_copy_word(f,0x108+i*4,source->render,render_dwords[i],4);
 return 0;
}
#endif
