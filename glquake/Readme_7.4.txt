
BlitzQuake 7.4

examples:

A way for ppc:
glquakeWOS_gcc_7 -windowmode -width 640 -noudp -ahi -mem 48 -fixboundbox -litfiles

Other options:
glquakeXXX_7	-width 640 -noudp
		-windowmode -width 800 -ahi -ip 192.168.0.2
		-width 800 -ahi -noudp -particles 1024 -fixboundbox -litfiles
		(litsupport seems to work only without gl_fake_multitextures)

		Increasing heapsize (-mem 32 or more) when using newer paks for normal quake avoids dma/cache sound issues:
		Basically load everything prior to game playing.

example for protocol 666/fitzquake (arcane dimensions mod version 1.42):

		-width 640 -ahi -noudp -mem 64 -protocol 666 -nocachetris -game ad +map ad_crucial


start parameter options (refer to old aminet blitzquake for the rest):


-startpri	Task Priority (if used, priority defaults to -3).

-wd		Remove window depth gadget.

-ddisc		(draw disc) Enable "disc loading icon", but does not work for some Voodoo gfx configurations.

-fpcr		For developers & also internal workaround for problems with Permedia Gfx in combination with 68k cpu.
		FPU rounding is kept 0 (longdoubleprecision/tonearest) for non Permedia gfx. 
		0x10 (longdoubleprecision/towardzero) is used when Permedia is detected. 
		The process is automatic without this option.

-drawfps	the old pq_drawfps

-quadtex	quad texture particles instead of circle particles.


New since 6.00:

-nocachetris	Rebuilds at loading level the mdls without using hard drive space. 
		It bypasses glquake/ directory (Usefull when textures are messed between different mods. 
		No need to erase glquake directory).

-fixboundbox	Fixes models dissapearing.

-protocol	Partial support for protocol 666 (fitzquake). By default 15 (Netquake/ Proquake).
		Increased heap is needed depending on mods. For example: "-mem 32 -protocol 666 -game shadows" 


autoexec.cfg file included

+mlook		the old mouse look

r_noshadow_list Contains a comma-separated list of filenames of models to exclude/include from shadow drawing.
		This "cvar" is hardcoded in the engine but can be modded if this list is included (autoexec.cfg)



2016/2017 console/cfg options:

gl_fogred, 
gl_foggreen, 
gl_fogblue, 
gl_fogstart, 
gl_fogend, 
gl_fogdensity	Parameters for gl_fog

	
r_shadows 1	Shadows with degree of fading towards ground (1.0 by default).

shadowalpha	Used in combination with r_shadows 1.

r_shadows 2	Old hardcoded alpha (0.5) and no check for distance to ground.

cl_gunfov 1	Fixes gun when > 90 field of view is used (fov).

r_bobitems	Bob !!

gl_conalpha	Console translucency.

sv_freezenonclients	Freeze all but you !!

protocol	Also featured in console. Needs reload of level.

max_edicts	"Entity dictionaries". Some protocol 666 mods use it automatically to increase their demands, 
		but others asume that enought edicts are at disposal by the engine (not the case with blitz).
		By default the value is 600 for protocol 15 (Netquake/Proquake), but higher values like 
		4096-8192(max) could be needed.
		Needs reload of level.

r_showbboxes	Draw boxes & points around entities (server side per edict physics bounding boxes). 
		1 or 2 values for depth_test on/off.

bboxcolor	Temporary color change for bboxes draw.

r_stereo, 
r_stereodepth	Untested. Needs gl_ztrick !!. Stereo view for anaglyph 3D visual (red/cyan glasses needed). 
		(-8,+8) Eye separation for r_stereo.
		(32..1024) Distance of converge r_stereodepth.

r_viewmodel_offset  Angle model gun (- or  +). 

2019:

gl_showpolys
gl_wireonly

Updated interpolation code: r_interpolations, key internationalization and some bugs.

added r_fullbright_list (for autoexec.cfg)

Also missing for latest release readme: gl_classic_particles - Warning use -particles 1024 maximum !!!

BSP2 support !!!

Added chase_transparent
Added gl_farclip - Increase to 16384 if there are problems with sky rendering (default 4096). Some mods increase it by themselfs. 

Notes:

68k users:	CopyMem patch is needed for Blitz !!! 
Voodoo gfx users:  -guardband start option seems to be needed for correct performance (not for Permedia !!).


SPECIAL NOTES:

New fixes for water lightmaps reduce a little the old performance framerate with gl_fake_multitexture (enabled by default). 
To increase fps, "gl_waterwarp 0" (lightmaps still work) or disable water lightmaps with "gl_fake_multitexture 2": The old 
original code/versions gave wrongly the same behaviour with 1 or 2.

In some cases (Warpos version), "gl_fake_multitexture 0" can be faster.

New 2019:

The use of newer models and sound samples (like Authentic Update Pak) with regular Quake can lead to random lockups when playing.
To get rid of (most of) those freezes just increase memory to higher values, see above all.

Developer mode now checks those issues that are caused by sound dma/memory when samples are not fully loaded at load level time.
