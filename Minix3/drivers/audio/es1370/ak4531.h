#ifndef AK4531_H
#define AK4531_H
/* best viewed with tabsize=4 */

#include "../../drivers.h"
#include <minix/sound.h>

_PROTOTYPE( int ak4531_init, (u16_t base, u16_t status_reg, u16_t bit, 
			u16_t poll) );
_PROTOTYPE( int ak4531_get_set_volume, (struct volume_level *level, int flag) );

#endif
