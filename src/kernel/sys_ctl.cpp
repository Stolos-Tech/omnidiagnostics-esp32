#include "sys_ctl.h"

static volatile bool s_reboot  = false;
static volatile bool s_passive = false;

void sys_request_reboot() { s_reboot = true; }
bool sys_take_reboot()    { if (!s_reboot) return false; s_reboot = false; return true; }

void sys_set_passive(bool on) { s_passive = on; }
bool sys_passive()            { return s_passive; }
