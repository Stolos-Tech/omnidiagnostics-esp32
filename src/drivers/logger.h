// Логер ОС: пише і в Serial, і в кільцевий буфер (log_ring) для веб-консолі.
#pragma once

void os_log(const char* fmt, ...);
