/*
==============
R_ClearSkyBox
==============
*/

extern float skymins[2][6], skymaxs[2][6];

void R_ClearSkyBox (void)
{
	int   i;

	for (i=0 ; i<6 ; i++)
	{
		skymins[0][i] = skymins[1][i] = 9999;
		skymaxs[0][i] = skymaxs[1][i] = -9999;
	}
}
