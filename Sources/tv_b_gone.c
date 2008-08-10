/******************************************************************************
*
* These TV POWER codes were compiled by Mitch Altman/Cornfield Electronics 
* for use with his TV-B-Gone product (http://www.tvbgone.com). In 2007, Mitch 
* joined forces with Limor Fried to release a kit version along with source 
* (http://www.ladyada.net/make/tvbgone/index.html). The portions used in this
* DC16 badge are distributed under Creative Commons 2.5 -- Attib & Share Alike
*
* Says Mitch from the TV-B-Gone kit main.c:
* "The tricky part of TV-B-Gone was collecting all of the POWER codes, and 
* getting rid of the duplicates and near-duplicates (because if there is a 
* duplicate, then one POWER code will turn a TV off, and the duplicate will 
* turn it on again (which we certainly do not want)."
*
* Thanks to Albert Yarusso (http://www.atariage.com) for helping with porting
* and debugging.
*
*******************************************************************************/
#include "FslTypes.h"
#include "DC16.h"


// Code 420 -- macs lol
static powercode appleCode  = {
  freq_to_timerval(37470), // 37.47 KHz  
{{907,453},
{70,70  },
{70,177 },
{70,177 },
{70,177 },
{70,70  },
{70,177 },
{70,177 },
{70,177 },
{70,177 },
{70,177 },
{70,177 },
{70,70  },
{70,70  },
{70,70  },
{70,70  },
{70,177 },
{70,70  },
{70,177 },
{70,70  },
{70,70  },
{70,70  },
{70,70  },
{70,70  },
{70,70  },
{70,177 }, //
{70,70  }, //
{70,70  }, //
{70,177 }, //
{70,177 }, //
{70,70  }, //
{70,70  }, //
{70,177 }, //
{70,0   }}
// end of code
  
};

// Code 420 -- hp laptops
static const powercode hpCode  = {
  freq_to_timerval(36130), // 37.47 KHz  
{{277,95},
{45,45  },
{45,45 },
{45,95 },
{45,95 },
{132,95  },
{45,45 },
{45,45 },
{45,45 },
{45,45 },
{45,45 },
{45,45 },
{45,45  },
{45,45  },
{45,45  },

{95,95 },
{45,45  },
{45,45 },
{95,95  },
{45,45  },
{45,45  },
{45,45  },
{45,45  },
{95,95  },
{45,45 },
{45,45  },
{45,45  },
{45,45 },
{45,45 },
{95,45  },
{45,95  },
{45,45 },
{45,0   }}
// end of code
  
};
  
const powercode *powerCodes[]  = {
&hpCode,
&appleCode,
&appleCode,
&appleCode,
&appleCode,
&appleCode,
&appleCode,
&appleCode
};


unsigned char num_codes = (sizeof(powerCodes)/sizeof(*powerCodes));
