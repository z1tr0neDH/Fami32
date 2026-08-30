#include "src_config.h"

int SAMP_RATE = 48000;
int ENG_SPEED = 60;
int LPF_CUTOFF = 18000;
int HPF_CUTOFF = 32;
int OVER_SAMPLE = 4;
#ifdef FAMI32_DESKTOP
const char *config_path = "./fami32_data/FM32CONF.CNF";
#else
const char *config_path = "/flash/FM32CONF.CNF";
#endif
