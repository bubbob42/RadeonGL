/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// gl_mesh.c: triangle model functions

#include "quakedef.h"

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

model_t *aliasmodel;
aliashdr_t *paliashdr;

int used[8192]; // was qboolean - Cowcat 2019

// the command list holds counts and s/t values that are valid for every frame
int	commands[8192];
int	numcommands;

// all frames will have their vertexes rearranged and expanded
// so they are in the order expected by the command list
int	vertexorder[8192];
int	numorder;
int	allverts, alltris;
int	stripverts[128];
int	striptris[128];
int	stripcount;

/*
================
StripLength
================
*/
static int StripLength (int starttri, int startv)
{
	int		m1, m2, j, k;
	mtriangle_t	*last, *check;

	used[starttri] = 2;

	last = &triangles[starttri];

	stripverts[0] = last->vertindex[(startv)%3];
	stripverts[1] = last->vertindex[(startv+1)%3];
	stripverts[2] = last->vertindex[(startv+2)%3];

	striptris[0] = starttri;
	stripcount = 1;

	m1 = last->vertindex[(startv+2)%3];
	m2 = last->vertindex[(startv+1)%3];

	// look for a matching triangle

nexttri:
	for (j=starttri+1, check=&triangles[starttri+1] ; j<pheader->numtris ; j++, check++)
	{
		if (check->facesfront != last->facesfront)
			continue;

		for (k=0 ; k<3 ; k++)
		{
			if (check->vertindex[k] != m1)
				continue;

			if (check->vertindex[ (k+1)%3 ] != m2)
				continue;

			// this is the next part of the fan

			// if we can't use this triangle, this tristrip is done
			if (used[j])
				goto done;

			// the new edge
			if (stripcount & 1)
				m2 = check->vertindex[ (k+2)%3 ];

			else
				m1 = check->vertindex[ (k+2)%3 ];

			stripverts[stripcount+2] = check->vertindex[ (k+2)%3 ];
			striptris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}
done:

	// clear the temp used flags
	for (j=starttri+1 ; j<pheader->numtris ; j++)
		if (used[j] == 2)
			used[j] = 0;

	return stripcount;
}

/*
===========
FanLength
===========
*/
static int FanLength (int starttri, int startv)
{
	int		m1, m2, j, k;
	mtriangle_t	*last, *check;

	used[starttri] = 2;

	last = &triangles[starttri];

	stripverts[0] = last->vertindex[(startv)%3];
	stripverts[1] = last->vertindex[(startv+1)%3];
	stripverts[2] = last->vertindex[(startv+2)%3];

	striptris[0] = starttri;
	stripcount = 1;

	m1 = last->vertindex[(startv+0)%3];
	m2 = last->vertindex[(startv+2)%3];

	// look for a matching triangle

nexttri:
	for (j=starttri+1, check=&triangles[starttri+1] ; j<pheader->numtris ; j++, check++)
	{
		if (check->facesfront != last->facesfront)
			continue;

		for (k=0 ; k<3 ; k++)
		{
			if (check->vertindex[k] != m1)
				continue;

			if (check->vertindex[ (k+1)%3 ] != m2)
				continue;

			// this is the next part of the fan

			// if we can't use this triangle, this tristrip is done
			if (used[j])
				goto done;

			// the new edge
			m2 = check->vertindex[ (k+2)%3 ];

			stripverts[stripcount+2] = m2;
			striptris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}
done:

	// clear the temp used flags
	for (j=starttri+1 ; j<pheader->numtris ; j++)
		if (used[j] == 2)
			used[j] = 0;

	return stripcount;
}


/*
================
BuildTris

Generate a list of trifans or strips
for the model, which holds for all frames
================
*/
static void BuildTris (void)
{
	int		i, j, k;
	int		startv;
	float		s, t;
	int		len, bestlen, besttype;
	int		bestverts[1024];
	int		besttris[1024];
	int		type;

	//
	// build tristrips
	//

	numorder = 0;
	numcommands = 0;
	memset (used, 0, sizeof(used));

	for (i=0 ; i<pheader->numtris ; i++)
	{
		// pick an unused triangle and start the trifan
		if (used[i])
			continue;

		bestlen = 0;
		besttype = 0;

		for (type = 0 ; type < 2 ; type++)
		//  type = 1;
		{
			for (startv =0 ; startv < 3 ; startv++)
			{
				if (type == 1)
					len = StripLength (i, startv);

				else
					len = FanLength (i, startv);

				if (len > bestlen)
				{
					besttype = type;
					bestlen = len;

					for (j=0 ; j<bestlen+2 ; j++)
						bestverts[j] = stripverts[j];

					for (j=0 ; j<bestlen ; j++)
						besttris[j] = striptris[j];
				}
			}
		}

		// mark the tris on the best strip as used
		for (j=0 ; j<bestlen ; j++)
			used[besttris[j]] = 1;

		if (besttype == 1)
			commands[numcommands++] = (bestlen+2);

		else
			commands[numcommands++] = -(bestlen+2);

		for (j=0 ; j<bestlen+2 ; j++)
		{
			int tmp;

			// emit a vertex into the reorder buffer
			k = bestverts[j];
			vertexorder[numorder++] = k;

			// emit s/t coords into the commands stream
			s = stverts[k].s;
			t = stverts[k].t;

			if (!triangles[besttris[0]].facesfront && stverts[k].onseam)
				s += pheader->skinwidth / 2;  // on back side

			s = (s + 0.5) / pheader->skinwidth;
			t = (t + 0.5) / pheader->skinheight;

			#if 0

			*(float *)&commands[numcommands++] = s;
			*(float *)&commands[numcommands++] = t;

			#else // test 2019 - Cowcat

			memcpy (&tmp, &s, 4);
			commands[numcommands++] = tmp;
			memcpy (&tmp, &t, 4);
			commands[numcommands++] = tmp;

			#endif
			
		}
	}

	commands[numcommands++] = 0;	// end of list marker

	Con_DPrintf ("%3i tri %3i vert %3i cmd\n", pheader->numtris, numorder, numcommands);

	allverts += numorder;
	alltris += pheader->numtris;
}


/*
================
GL_BuildAliasBatch

2026-09-14. The command list is dozens of short triangle strips and fans, and
every one reaches the GPU as a draw of its own (glDrawArrays per strip in
GL_DrawAliasArrayFrame), each with its own vertex buffers and state. This turns
the strips and fans into one triangle list over the model's distinct
(vertex, s, t) combinations, so GL_DrawAliasBatch (gl_rmain.c) can draw an
entity with one glDrawElements(GL_TRIANGLES) inside glLockArrays, MiniGLV3D's
indexed path. Quake2's models take the same path (ref_gl_lib gl_mesh.c).

Hardware (static glquake v218): demo1 timedemo 61 -> 79 fps against the
glDrawArrays path, models unchanged.

A command-list vertex has no index of its own: the n-th vertex of the list is
the n-th vertex of every pose, copied from model vertex vertexorder[n]. So
vertices are merged on (vertexorder[n], s, t), and each batch vertex keeps one
such n to read its pose data from.

A model with more distinct vertices than ALIASBATCH_MAX_VERTS is split into
chunks of at most that many, one draw each. Anything unexpected, or no memory
for the build's scratch, leaves numbatchchunks 0 and the model on its strips
and fans.
================
*/
static void GL_BuildAliasBatch (void)
{
	static int		head[MAXALIASVERTS];		// newest batch vertex of this model vertex in the chunk
	static int		headstamp[MAXALIASVERTS];	// the chunk head[] was set in
	static int		stamp;
	static int		elem[ALIASBATCH_MAX_VERTS];
	int			*next;				// older batch vertex of the same model vertex
	short			*bverts;
	float			*bst;
	unsigned short		*bindexes;
	aliasbatchchunk_t	*bchunks, *chunk = NULL;
	int		numchunks = 0, numverts = 0, numindexes = 0;
	int		*o = commands;
	int		*end = commands + numcommands;
	int		n = 0;		// command-list vertex number
	int		count, i;
	qboolean	fan;
	byte		*p;

	paliashdr->numbatchchunks = 0;

	if (numorder < 3 || numorder > (int)(sizeof(vertexorder)/sizeof(vertexorder[0])))
		return;

	// scratch for this model only, freed below: a strip of count vertices adds
	// at most count batch vertices, 3*count indexes and one chunk
	next = malloc (numorder * sizeof(int));
	bverts = malloc (numorder * sizeof(short));
	bst = malloc (numorder * 2 * sizeof(float));
	bindexes = malloc (numorder * 3 * sizeof(unsigned short));
	bchunks = malloc ((numorder / 3 + 1) * sizeof(aliasbatchchunk_t));

	if (!next || !bverts || !bst || !bindexes || !bchunks)
		goto done;

	while (1)
	{
		if (o >= end)
			goto done;		// no end marker

		count = *o++;

		if (!count)
			break;

		fan = count < 0;

		if (fan)
			count = -count;

		if (count > ALIASBATCH_MAX_VERTS || count > (end - o) / 2 || count > numorder - n)
			goto done;

		if (count >= 3)
		{
			// a new chunk if the strip's vertices may not fit
			if (!chunk || chunk->numverts + count > ALIASBATCH_MAX_VERTS)
			{
				chunk = &bchunks[numchunks++];
				chunk->firstvert = numverts;
				chunk->numverts = 0;
				chunk->firstindex = numindexes;
				chunk->numindexes = 0;
				stamp++;
			}

			for (i=0 ; i<count ; i++)
			{
				float	s = ((float *)o)[i*2];
				float	t = ((float *)o)[i*2+1];
				int	k = vertexorder[n+i];
				int	v;

				if ((unsigned)k >= (unsigned)paliashdr->numverts || k >= MAXALIASVERTS)
					goto done;

				if (headstamp[k] != stamp)
				{
					headstamp[k] = stamp;
					head[k] = -1;
				}

				for (v = head[k] ; v >= 0 ; v = next[v])
					if (bst[v*2] == s && bst[v*2+1] == t)
						break;

				if (v < 0)
				{
					v = numverts++;
					bverts[v] = n + i;
					bst[v*2] = s;
					bst[v*2+1] = t;
					next[v] = head[k];
					head[k] = v;
					chunk->numverts++;
				}

				elem[i] = v - chunk->firstvert;
			}

			// GL's vertex order for strips and fans, so each triangle keeps its facing
			for (i=0 ; i<count-2 ; i++)
			{
				unsigned short	*idx = bindexes + numindexes + i*3;

				if (fan)
				{
					idx[0] = elem[0];
					idx[1] = elem[i+1];
					idx[2] = elem[i+2];
				}

				else if (i & 1)
				{
					idx[0] = elem[i+1];
					idx[1] = elem[i];
					idx[2] = elem[i+2];
				}

				else
				{
					idx[0] = elem[i];
					idx[1] = elem[i+1];
					idx[2] = elem[i+2];
				}
			}

			numindexes += (count-2) * 3;
			chunk->numindexes += (count-2) * 3;
		}

		o += count * 2;
		n += count;
	}

	if (!numindexes)
		goto done;

	// on the hunk after the pose data, as offsets: the model moves to the cache whole

	p = Hunk_Alloc (numchunks * sizeof(aliasbatchchunk_t));
	paliashdr->batchchunks = p - (byte *)paliashdr;
	memcpy (p, bchunks, numchunks * sizeof(aliasbatchchunk_t));

	p = Hunk_Alloc (numverts * sizeof(short));
	paliashdr->batchverts = p - (byte *)paliashdr;
	memcpy (p, bverts, numverts * sizeof(short));

	p = Hunk_Alloc (numverts * 2 * sizeof(float));
	paliashdr->batchst = p - (byte *)paliashdr;
	memcpy (p, bst, numverts * 2 * sizeof(float));

	p = Hunk_Alloc (numindexes * sizeof(unsigned short));
	paliashdr->batchindexes = p - (byte *)paliashdr;
	memcpy (p, bindexes, numindexes * sizeof(unsigned short));

	paliashdr->numbatchchunks = numchunks;

done:
	free (next);
	free (bverts);
	free (bst);
	free (bindexes);
	free (bchunks);
}


/*
================
GL_MakeAliasModelDisplayLists
================
*/
extern qboolean cachetris;

void GL_MakeAliasModelDisplayLists (model_t *m, aliashdr_t *hdr)
{
	int		i, j;
	int		*cmds;
	trivertx_t	*verts;
	char		cache[MAX_QPATH], fullpath[MAX_OSPATH];
	FILE		*f;
	
	aliasmodel = m;
	paliashdr = hdr;  // (aliashdr_t *)Mod_Extradata (m);

	if (!cachetris)
	{ 
		//Con_Printf ("meshing %s...\n",m->name);
		BuildTris ();	// trifans or lists
	}
	
	else
	{
		//
		// look for a cached version
		//

		strcpy (cache, "glquake/");
		COM_StripExtension (m->name+strlen("progs/"), cache+strlen("glquake/"));
		strcat (cache, ".ms2");

		COM_FOpenFile (cache, &f);
  
		if (f)
		{
			fread (&numcommands, 4, 1, f);
			fread (&numorder, 4, 1, f);
			fread (&commands, numcommands * sizeof(commands[0]), 1, f);
			fread (&vertexorder, numorder * sizeof(vertexorder[0]), 1, f);
			fclose (f);
		}

		else
		{
			//
			// build it from scratch
			//

			Con_Printf ("meshing %s...\n", m->name);
			BuildTris ();	// trifans or lists

			//
			// save out the cached version
			//

			sprintf (fullpath, "%s/%s", com_gamedir, cache);

			f = fopen (fullpath, "wb");

			if (f)
			{
				fwrite (&numcommands, 4, 1, f);
				fwrite (&numorder, 4, 1, f);
				fwrite (&commands, numcommands * sizeof(commands[0]), 1, f);
				fwrite (&vertexorder, numorder * sizeof(vertexorder[0]), 1, f);
				fclose (f);
			}
		}

	}

	// save the data out

	paliashdr->poseverts = numorder;

	cmds = Hunk_Alloc (numcommands * 4);
	paliashdr->commands = (byte *)cmds - (byte *)paliashdr;
	memcpy (cmds, commands, numcommands * 4);

	verts = Hunk_Alloc (paliashdr->numposes * paliashdr->poseverts * sizeof(trivertx_t) );
	paliashdr->posedata = (byte *)verts - (byte *)paliashdr;

	for (i=0 ; i<paliashdr->numposes ; i++)
		for (j=0 ; j<numorder ; j++)
			*verts++ = poseverts[i][vertexorder[j]];

	GL_BuildAliasBatch ();
}

