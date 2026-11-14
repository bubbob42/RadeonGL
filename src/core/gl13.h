/*
 * gl13.h -- the GL 1.2 / 1.3 and extension values the core's phase 1 takes
 * (tex13.c, 2026-11-12) that neither the MiniGL SDK's gl.h nor glextra.h
 * defines. Values from the GL 1.3 specification and the extension registry
 * (ARB_texture_env_combine/dot3/crossbar, ATI_texture_env_combine3,
 * ARB_texture_border_clamp, ARB_texture_mirrored_repeat,
 * EXT_texture_filter_anisotropic, SGIS_texture_lod, EXT_texture_lod_bias,
 * ARB_texture_cube_map, EXT_bgra, EXT_packed_pixels,
 * EXT_separate_specular_color, EXT_rescale_normal).
 */

#ifndef CORE_GL13_H
#define CORE_GL13_H

#ifndef GL_COMBINE
#define GL_COMBINE                           0x8570UL
#endif
#ifndef GL_COMBINE_RGB
#define GL_COMBINE_RGB                       0x8571UL
#endif
#ifndef GL_COMBINE_ALPHA
#define GL_COMBINE_ALPHA                     0x8572UL
#endif
#ifndef GL_RGB_SCALE
#define GL_RGB_SCALE                         0x8573UL
#endif
#ifndef GL_ADD_SIGNED
#define GL_ADD_SIGNED                        0x8574UL
#endif
#ifndef GL_INTERPOLATE
#define GL_INTERPOLATE                       0x8575UL
#endif
#ifndef GL_CONSTANT
#define GL_CONSTANT                          0x8576UL
#endif
#ifndef GL_PRIMARY_COLOR
#define GL_PRIMARY_COLOR                     0x8577UL
#endif
#ifndef GL_PREVIOUS
#define GL_PREVIOUS                          0x8578UL
#endif
#ifndef GL_SOURCE0_RGB
#define GL_SOURCE0_RGB                       0x8580UL
#endif
#ifndef GL_SOURCE1_RGB
#define GL_SOURCE1_RGB                       0x8581UL
#endif
#ifndef GL_SOURCE2_RGB
#define GL_SOURCE2_RGB                       0x8582UL
#endif
#ifndef GL_SOURCE0_ALPHA
#define GL_SOURCE0_ALPHA                     0x8588UL
#endif
#ifndef GL_SOURCE1_ALPHA
#define GL_SOURCE1_ALPHA                     0x8589UL
#endif
#ifndef GL_SOURCE2_ALPHA
#define GL_SOURCE2_ALPHA                     0x858aUL
#endif
#ifndef GL_OPERAND0_RGB
#define GL_OPERAND0_RGB                      0x8590UL
#endif
#ifndef GL_OPERAND1_RGB
#define GL_OPERAND1_RGB                      0x8591UL
#endif
#ifndef GL_OPERAND2_RGB
#define GL_OPERAND2_RGB                      0x8592UL
#endif
#ifndef GL_OPERAND0_ALPHA
#define GL_OPERAND0_ALPHA                    0x8598UL
#endif
#ifndef GL_OPERAND1_ALPHA
#define GL_OPERAND1_ALPHA                    0x8599UL
#endif
#ifndef GL_OPERAND2_ALPHA
#define GL_OPERAND2_ALPHA                    0x859aUL
#endif
#ifndef GL_SUBTRACT
#define GL_SUBTRACT                          0x84e7UL
#endif
#ifndef GL_DOT3_RGB
#define GL_DOT3_RGB                          0x86aeUL
#endif
#ifndef GL_DOT3_RGBA
#define GL_DOT3_RGBA                         0x86afUL
#endif
#ifndef GL_DOT3_RGB_EXT
#define GL_DOT3_RGB_EXT                      0x8740UL
#endif
#ifndef GL_DOT3_RGBA_EXT
#define GL_DOT3_RGBA_EXT                     0x8741UL
#endif
#ifndef GL_MODULATE_ADD_ATI
#define GL_MODULATE_ADD_ATI                  0x8744UL
#endif
#ifndef GL_MODULATE_SIGNED_ADD_ATI
#define GL_MODULATE_SIGNED_ADD_ATI           0x8745UL
#endif
#ifndef GL_MODULATE_SUBTRACT_ATI
#define GL_MODULATE_SUBTRACT_ATI             0x8746UL
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R                    0x8072UL
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR              0x1004UL
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER                   0x812dUL
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT                   0x8370UL
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT        0x84feUL
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT    0x84ffUL
#endif
#ifndef GL_TEXTURE_MIN_LOD
#define GL_TEXTURE_MIN_LOD                   0x813aUL
#endif
#ifndef GL_TEXTURE_MAX_LOD
#define GL_TEXTURE_MAX_LOD                   0x813bUL
#endif
#ifndef GL_TEXTURE_BASE_LEVEL
#define GL_TEXTURE_BASE_LEVEL                0x813cUL
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL                 0x813dUL
#endif
#ifndef GL_TEXTURE_FILTER_CONTROL_EXT
#define GL_TEXTURE_FILTER_CONTROL_EXT        0x8500UL
#endif
#ifndef GL_TEXTURE_LOD_BIAS_EXT
#define GL_TEXTURE_LOD_BIAS_EXT              0x8501UL
#endif
#ifndef GL_MAX_TEXTURE_LOD_BIAS_EXT
#define GL_MAX_TEXTURE_LOD_BIAS_EXT          0x84fdUL
#endif
#ifndef GL_NORMAL_MAP
#define GL_NORMAL_MAP                        0x8511UL
#endif
#ifndef GL_REFLECTION_MAP
#define GL_REFLECTION_MAP                    0x8512UL
#endif
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP                  0x8513UL
#endif
#ifndef GL_TEXTURE_BINDING_CUBE_MAP
#define GL_TEXTURE_BINDING_CUBE_MAP          0x8514UL
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X       0x8515UL
#endif
#ifndef GL_TEXTURE_CUBE_MAP_NEGATIVE_X
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X       0x8516UL
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_Y
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y       0x8517UL
#endif
#ifndef GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y       0x8518UL
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_Z
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z       0x8519UL
#endif
#ifndef GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z       0x851aUL
#endif
#ifndef GL_PROXY_TEXTURE_CUBE_MAP
#define GL_PROXY_TEXTURE_CUBE_MAP            0x851bUL
#endif
#ifndef GL_MAX_CUBE_MAP_TEXTURE_SIZE
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE         0x851cUL
#endif
#ifndef GL_S
#define GL_S                                 0x2000UL
#endif
#ifndef GL_T
#define GL_T                                 0x2001UL
#endif
#ifndef GL_R
#define GL_R                                 0x2002UL
#endif
#ifndef GL_Q
#define GL_Q                                 0x2003UL
#endif
#ifndef GL_BGR
#define GL_BGR                               0x80e0UL
#endif
#ifndef GL_BGRA
#define GL_BGRA                              0x80e1UL
#endif
#ifndef GL_UNSIGNED_BYTE_3_3_2
#define GL_UNSIGNED_BYTE_3_3_2               0x8032UL
#endif
#ifndef GL_UNSIGNED_SHORT_4_4_4_4
#define GL_UNSIGNED_SHORT_4_4_4_4            0x8033UL
#endif
#ifndef GL_UNSIGNED_SHORT_5_5_5_1
#define GL_UNSIGNED_SHORT_5_5_5_1            0x8034UL
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8
#define GL_UNSIGNED_INT_8_8_8_8              0x8035UL
#endif
#ifndef GL_UNSIGNED_INT_10_10_10_2
#define GL_UNSIGNED_INT_10_10_10_2           0x8036UL
#endif
#ifndef GL_UNSIGNED_BYTE_2_3_3_REV
#define GL_UNSIGNED_BYTE_2_3_3_REV           0x8362UL
#endif
#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5              0x8363UL
#endif
#ifndef GL_UNSIGNED_SHORT_5_6_5_REV
#define GL_UNSIGNED_SHORT_5_6_5_REV          0x8364UL
#endif
#ifndef GL_UNSIGNED_SHORT_4_4_4_4_REV
#define GL_UNSIGNED_SHORT_4_4_4_4_REV        0x8365UL
#endif
#ifndef GL_UNSIGNED_SHORT_1_5_5_5_REV
#define GL_UNSIGNED_SHORT_1_5_5_5_REV        0x8366UL
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV          0x8367UL
#endif
#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV       0x8368UL
#endif
#ifndef GL_R3_G3_B2
#define GL_R3_G3_B2                          0x2a10UL
#endif
#ifndef GL_ALPHA4
#define GL_ALPHA4                            0x803bUL
#endif
#ifndef GL_ALPHA12
#define GL_ALPHA12                           0x803dUL
#endif
#ifndef GL_ALPHA16
#define GL_ALPHA16                           0x803eUL
#endif
#ifndef GL_LUMINANCE4
#define GL_LUMINANCE4                        0x803fUL
#endif
#ifndef GL_LUMINANCE12
#define GL_LUMINANCE12                       0x8041UL
#endif
#ifndef GL_LUMINANCE16
#define GL_LUMINANCE16                       0x8042UL
#endif
#ifndef GL_LUMINANCE4_ALPHA4
#define GL_LUMINANCE4_ALPHA4                 0x8043UL
#endif
#ifndef GL_LUMINANCE6_ALPHA2
#define GL_LUMINANCE6_ALPHA2                 0x8044UL
#endif
#ifndef GL_LUMINANCE8_ALPHA8
#define GL_LUMINANCE8_ALPHA8                 0x8045UL
#endif
#ifndef GL_LUMINANCE12_ALPHA4
#define GL_LUMINANCE12_ALPHA4                0x8046UL
#endif
#ifndef GL_LUMINANCE12_ALPHA12
#define GL_LUMINANCE12_ALPHA12               0x8047UL
#endif
#ifndef GL_LUMINANCE16_ALPHA16
#define GL_LUMINANCE16_ALPHA16               0x8048UL
#endif
#ifndef GL_INTENSITY4
#define GL_INTENSITY4                        0x804aUL
#endif
#ifndef GL_INTENSITY12
#define GL_INTENSITY12                       0x804cUL
#endif
#ifndef GL_INTENSITY16
#define GL_INTENSITY16                       0x804dUL
#endif
#ifndef GL_RGB4
#define GL_RGB4                              0x804fUL
#endif
#ifndef GL_RGB10
#define GL_RGB10                             0x8052UL
#endif
#ifndef GL_RGB12
#define GL_RGB12                             0x8053UL
#endif
#ifndef GL_RGB16
#define GL_RGB16                             0x8054UL
#endif
#ifndef GL_RGBA2
#define GL_RGBA2                             0x8055UL
#endif
#ifndef GL_RGBA4
#define GL_RGBA4                             0x8056UL
#endif
#ifndef GL_RGB10_A2
#define GL_RGB10_A2                          0x8059UL
#endif
#ifndef GL_RGBA12
#define GL_RGBA12                            0x805aUL
#endif
#ifndef GL_RGBA16
#define GL_RGBA16                            0x805bUL
#endif
#ifndef GL_LIGHT_MODEL_COLOR_CONTROL
#define GL_LIGHT_MODEL_COLOR_CONTROL         0x81f8UL
#endif
#ifndef GL_SINGLE_COLOR
#define GL_SINGLE_COLOR                      0x81f9UL
#endif
#ifndef GL_SEPARATE_SPECULAR_COLOR
#define GL_SEPARATE_SPECULAR_COLOR           0x81faUL
#endif
#ifndef GL_RESCALE_NORMAL
#define GL_RESCALE_NORMAL                    0x803aUL
#endif
#ifndef GL_TEXTURE1_ARB
#define GL_TEXTURE1_ARB                      0x84c1UL
#endif
#ifndef GL_TEXTURE3_ARB
#define GL_TEXTURE3_ARB                      0x84c3UL
#endif
#ifndef GL_TEXTURE4_ARB
#define GL_TEXTURE4_ARB                      0x84c4UL
#endif
#ifndef GL_TEXTURE5_ARB
#define GL_TEXTURE5_ARB                      0x84c5UL
#endif

#endif /* CORE_GL13_H */
