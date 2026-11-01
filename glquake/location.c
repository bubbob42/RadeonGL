//
// location.c
//
// JPG
// 
// This entire file is new in proquake.  It is used to translate map areas
// to names for the %l formatting specifier
//

#include "quakedef.h"

#define MAX_LOCATIONS 64

location_t  locations[MAX_LOCATIONS];
int     numlocations = 0;

/*
===============
LOC_LoadLocations

Load the locations for the current level from the location file
===============
*/
void LOC_LoadLocations (void)
{
  FILE *f;
  char *mapname, *ch;
  char filename[64] = "locs/";
  char buff[256];
  location_t *l;
  int i;
  float temp;

  numlocations = 0;
  mapname = cl.worldmodel->name;
  if (Q_strncasecmp(mapname, "maps/", 5))
    return;
  Q_strcpy(filename + 5, mapname + 5);
  ch = Q_strrchr(filename, '.');
  if (ch)
    *ch = 0;
  Q_strcat(filename, ".loc");

  COM_FOpenFile(filename, &f);
  if (!f)
    return;

  l = locations;
  while (!feof(f) && numlocations < MAX_LOCATIONS)
  {
    if (fscanf(f, "%f, %f, %f, %f, %f, %f, ", &l->a[0], &l->a[1], &l->a[2], &l->b[0], &l->b[1], &l->b[2]) == 6)
    {
      for (i = 0 ; i < 3 ; i++)
      {
        if (l->a[i] > l->b[i])
        {
          temp = l->a[i];
          l->a[i] = l->b[i];
          l->b[i] = temp;
        }
      }
      l->a[2] -= 32.0;
      l->b[2] += 32.0;
      fgets(buff, 256, f);

      ch = Q_strrchr(buff, '\n');
      if (ch)
        *ch = 0;
      ch = Q_strrchr(buff, '\"');
      if (ch)
        *ch = 0;
      for (ch = buff ; *ch == ' ' || *ch == '\t' || *ch == '\"' ; ch++);
      Q_strncpy(l->name, ch, 31);
      l = &locations[++numlocations];
    }
    else
      fgets(buff, 256, f);
  }

  fclose(f);
}

/*
===============
LOC_GetLocation

Get the name of the location of a point
===============
*/
char *LOC_GetLocation (vec3_t p)
{
  location_t *l;

  for (l = locations ; l < locations + numlocations ; l++)
  {
    if (l->a[0] <= p[0] && p[0] <= l->b[0] && l->a[1] <= p[1] && p[1] <= l->b[1] && l->a[2] <= p[2] && p[2] <= l->b[2])
      return l->name;
  }

  return "somewhere";
}

