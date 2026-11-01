/* gfxboard-software specific driver functions (prototypes) */

extern void VID_SetPalette_ChunkyPPC (unsigned char *);
extern void VID_ShiftPalette_ChunkyPPC (unsigned char *);
extern void VID_Init_ChunkyPPC (unsigned char *);
extern void VID_Shutdown_ChunkyPPC (void);
extern void VID_Update_ChunkyPPC (vrect_t *);
extern void SysSendKeyEvents_ChunkyPPC(void);

extern void VID_SetPalette_CGFX (unsigned char *);
extern void VID_ShiftPalette_CGFX (unsigned char *);
extern void VID_Init_CGFX (unsigned char *);
extern void VID_Shutdown_CGFX (void);
extern void VID_Update_CGFX (vrect_t *);
extern void SysSendKeyEvents_CGFX(void);

extern void VID_SetPalette_AGA (unsigned char *);
extern void VID_ShiftPalette_AGA (unsigned char *);
extern void VID_Init_AGA (unsigned char *);
extern void VID_Shutdown_AGA (void);
extern void VID_Update_AGA (vrect_t *);
extern void SysSendKeyEvents_AGA(void);
