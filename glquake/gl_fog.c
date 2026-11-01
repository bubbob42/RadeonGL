
#include "quakedef.h"

cvar_t	gl_fog = {"gl_fog", "0", true}; // true Cowcat
cvar_t	gl_fogstart = {"gl_fogstart", "256.0", true}; // added 2016 Cowcat
cvar_t	gl_fogend = {"gl_fogend", "2048.0", true};
cvar_t	gl_fogred = {"gl_fogred", "0.3", true};
cvar_t	gl_foggreen = {"gl_foggreen","0.3", true};
cvar_t	gl_fogblue = {"gl_fogblue","0.3", true};
cvar_t	gl_fogdensity = {"gl_fogdensity","0.2", true}; //
qboolean fog;

void R_Fog_BeginFrame(void)
{
	//GLfloat fogcolors[3];

	if (gl_fog.value)
	{
		/* FOUR floats: glFogfv(GL_FOG_COLOR) reads RGBA. Alpha is 1.0 and
		 * has no effect -- GL's fog blend touches R, G and B only -- but the
		 * driver reads the fourth element, so it has to exist. */
		GLfloat fogcolors[4] = { (GLfloat) gl_fogred.value, (GLfloat) gl_foggreen.value, (GLfloat) gl_fogblue.value, 1.0f };
		glFogi(GL_FOG_MODE, GL_LINEAR);

		//fogcolors[0] = gl_fogred.value;
		//fogcolors[1] = gl_foggreen.value;
		//fogcolors[2] = gl_fogblue.value;
		
		glFogfv(GL_FOG_COLOR, fogcolors);

		glFogf(GL_FOG_START, gl_fogstart.value);
		glFogf(GL_FOG_END, gl_fogend.value);
		glFogf(GL_FOG_DENSITY, gl_fogdensity.value); //surgeon
			
		glEnable(GL_FOG);

		fog = true; // Cowcat
	}
}

void R_Fog_EndFrame(void)
{
	if (fog) // Cowcat
	{	
		glDisable(GL_FOG);
		fog = false;
	}
}
