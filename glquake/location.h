//
// location.h
//
// JPG
// 
// This entire file is new in proquake.  It is used to translate map areas
// to names for the %l formatting specifier
//

typedef struct 
{
  vec3_t a;   // min xyz corner
  vec3_t b;   // max xyz corner
  char name[32];
} location_t;

// Load the locations for the current level from the location file
void LOC_LoadLocations (void);

// Get the name of the location of a point
char *LOC_GetLocation (vec3_t p);

